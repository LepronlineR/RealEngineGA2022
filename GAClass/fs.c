#include "fs.h"

#include "event.h"
#include "heap.h"
#include "queue.h"
#include "thread.h"
#include "debug.h"

#include <stddef.h>
#include <stdio.h>
#include "include/lz4/lz4.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct fs_t {
	heap_t* heap;
	queue_t* file_queue;
	thread_t* file_thread;
	queue_t* compression_file_queue;
	thread_t* compression_file_thread;
} fs_t;

typedef enum fs_work_op_t {
	k_fs_work_op_read,
	k_fs_work_op_write,
} fs_work_op_t;

typedef struct fs_work_t {
	heap_t* heap;
	fs_work_op_t op;
	char path[1024];
	bool null_terminate;
	bool use_compression;
	char* buffer;
	size_t size;
	size_t compressed_size;
	event_t* done;
	int result;
} fs_work_t;

static int file_thread_func(void* user);
static int compress_thread_func(void* user);

fs_t* fs_create(heap_t* heap, int queue_capacity) {
	fs_t* fs = heap_alloc(heap, sizeof(fs_t), 8);
	fs->heap = heap;
	fs->file_queue = queue_create(heap, queue_capacity);
	fs->file_thread = thread_create(file_thread_func, fs);
	// Create the compressor thread and queue for file compression/decompression
	fs->compression_file_queue = queue_create(heap, queue_capacity);
	fs->compression_file_thread = thread_create(compress_thread_func, fs);
	return fs;
}

void fs_destroy(fs_t* fs) {
	// remove the compressor/decompressor
	queue_push(fs->compression_file_queue, NULL);
	thread_destroy(fs->compression_file_thread);
	queue_destroy(fs->compression_file_queue);
	// remove everything else
	queue_push(fs->file_queue, NULL);
	thread_destroy(fs->file_thread);
	queue_destroy(fs->file_queue);
	heap_free(fs->heap, fs);
}

fs_work_t* fs_read(fs_t* fs, const char* path, heap_t* heap, bool null_terminate, bool use_compression) {
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->heap = heap;
	work->op = k_fs_work_op_read;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = NULL;
	work->size = 0;
	work->compressed_size = 0;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = null_terminate;
	work->use_compression = use_compression;
	queue_push(fs->file_queue, work);
	return work;
}

fs_work_t* fs_write(fs_t* fs, const char* path, const void* buffer, size_t size, bool use_compression) {
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->heap = fs->heap;
	work->op = k_fs_work_op_write;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = (char*)buffer;
	work->size = size;
	work->compressed_size = 0;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = false;
	work->use_compression = use_compression;

	if (use_compression) { // HOMEWORK 2: Queue file write work on compression queue!
		queue_push(fs->compression_file_queue, work);
	} else {
		queue_push(fs->file_queue, work);
	}

	return work;
}

bool fs_work_is_done(fs_work_t* work) {
	return work ? event_is_raised(work->done) : true;
}

void fs_work_wait(fs_work_t* work) {
	if (work) {
		event_wait(work->done);
	}
}

int fs_work_get_result(fs_work_t* work) {
	fs_work_wait(work);
	return work ? work->result : -1;
}

void* fs_work_get_buffer(fs_work_t* work) {
	fs_work_wait(work);
	return work ? work->buffer : NULL;
}

size_t fs_work_get_size(fs_work_t* work) {
	fs_work_wait(work);
	return work ? work->size : 0;
}

void fs_work_destroy(fs_work_t* work) {
	if (work) {
		event_wait(work->done);
		event_destroy(work->done);
		heap_free(work->heap, work);
	}
}

static void file_read(fs_t* fs, fs_work_t* work) {
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0) {
		work->result = -1;
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		work->result = GetLastError();
		return;
	}

	if (!GetFileSizeEx(handle, (PLARGE_INTEGER)&work->size)) {
		work->result = GetLastError();
		CloseHandle(handle);
		return;
	}

	work->buffer = heap_alloc(work->heap, work->null_terminate ? work->size + 1 : work->size, 8);

	DWORD bytes_read = 0;
	if (!ReadFile(handle, work->buffer, (DWORD)work->size, &bytes_read, NULL)) {
		work->result = GetLastError();
		CloseHandle(handle);
		return;
	}

	work->size = bytes_read;

	if (work->null_terminate) {
		((char*)work->buffer)[bytes_read] = 0;
	}

	CloseHandle(handle);

	if (work->use_compression) { // HOMEWORK 2: Queue file read work on decompression queue!
		queue_push(fs->compression_file_queue, work);
	} else {
		event_signal(work->done);
	}
}

static void file_write(fs_work_t* work) {
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0) {
		work->result = -1;
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		work->result = GetLastError();
		return;
	}

	/*
	// if compression is used, write the compression size into the file for reading
	DWORD bytes_written_compression = 0;
	char compressed_size_str[256];
	if (work->use_compression) {
		snprintf(compressed_size_str, sizeof(compressed_size_str), "%zu ", work->compressed_size);
		if (!WriteFile(handle, (LPVOID) compressed_size_str, strlen(compressed_size_str), &bytes_written_compression, NULL)) {
			work->result = GetLastError();
			CloseHandle(handle);
			return;
		}
	}
	*/

	DWORD bytes_written = 0;
	if (!WriteFile(handle, work->buffer, (DWORD)work->size, &bytes_written, NULL)) {
		work->result = GetLastError();
		CloseHandle(handle);
		return;
	}

	work->size = bytes_written;

	CloseHandle(handle);

	if (work->use_compression) {
		// free the buffer (we don't need the compressed buffer)
		heap_free(work->heap, work->buffer);
	}

	event_signal(work->done);
}

static void file_read_compressed(fs_work_t* work) {
	// read the file for the destination file size (delimit by ' ')
	FILE* stream;
	errno_t err = fopen_s(&stream, work->path, "r");
	if (err) {
		debug_print_line(k_print_error, "Unable to open the file for reading a compressed file.\n");
		return;
	}
	int space_count = 0; // count the amount of space to skip for the buffer
	int dst_buffer_size = work->size;
	int compressed_size = 0;
	fscanf_s(stream, "%d%n", &compressed_size, &space_count);
	// move in compressed_size + 1, due to the extra space added to the compressed file
	space_count += 1;
	
	void* dst_buffer = heap_alloc(work->heap, dst_buffer_size, 0);
	int decompressed_size = LZ4_decompress_safe((char*)work->buffer+space_count, dst_buffer, compressed_size, dst_buffer_size);

	// there is extra garbage left over from writing into the file
	//	i.e. if we write 4 bytes at the start, there are 4 garbage bytes at the end that we need to clear up
	for (int x = decompressed_size + space_count; x > decompressed_size-1; x--) {
		*((char*)dst_buffer+x) = '\0';
	}

	heap_free(work->heap, work->buffer);

	work->buffer = dst_buffer;
	work->size = decompressed_size;

	event_signal(work->done);
}

static void file_write_compressed(fs_t* fs, fs_work_t* work) {
	int dst_buffer_size = LZ4_compressBound(work->size);
	char* dst_buffer = heap_alloc(work->heap, dst_buffer_size + sizeof(int), 8);
	int compressed_size = LZ4_compress_default(work->buffer, dst_buffer + sizeof(int), (int)work->size, dst_buffer_size);
	memcpy(dst_buffer, compressed_size, sizeof(int));
	
	work->buffer = dst_buffer;
	work->compressed_size = compressed_size;
	queue_push(fs->file_queue, work);
}

static int file_thread_func(void* user) {
	fs_t* fs = user;
	while (true) {
		fs_work_t* work = queue_pop(fs->file_queue);
		if (work == NULL){
			break;
		}

		switch (work->op) {
		case k_fs_work_op_read:
			file_read(fs, work);
			break;
		case k_fs_work_op_write:
			file_write(work);
			break;
		}
	}
	return 0;
}

static int compress_thread_func(void* user) {
	fs_t* fs = user;
	while (true) {
		fs_work_t* work = queue_pop(fs->compression_file_queue);
		if (work == NULL) {
			break;
		}

		switch (work->op) {
		case k_fs_work_op_read:
			file_read_compressed(work);
			break;
		case k_fs_work_op_write:
			file_write_compressed(fs, work);
			break;
		}
	}
	return 0;
}