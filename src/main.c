#include "atomic.h"
#include "debug.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "mat4f.h"
#include "mutex.h"
#include "quatf.h"
#include "thread.h"
#include "timer.h"
#include "timer_object.h"
#include "trace.h"
#include "vec3f.h"
#include "wm.h"

#include <assert.h>
#include <stdio.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include <windows.h>

static void homework3_test();

int main(int argc, const char* argv[])
{
	debug_install_exception_handler();
	timer_startup();

	homework3_test();

	debug_set_print_mask(k_print_info | k_print_warning | k_print_error);

	heap_t* heap = heap_create(2 * 1024 * 1024);
	fs_t* fs = fs_create(heap, 8);
	wm_window_t* window = wm_create(heap);
	gpu_t* gpu = gpu_create(heap, window);
	timer_object_t* root_time = timer_object_create(heap, NULL);

	// Setup for rendering a cube!
	fs_work_t* vertex_shader_work = fs_read(fs, "shaders/triangle.vert.spv", heap, false, false);
	fs_work_t* fragment_shader_work = fs_read(fs, "shaders/triangle.frag.spv", heap, false, false);
	gpu_shader_info_t shader_info =
	{
		.vertex_shader_data = fs_work_get_buffer(vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(fragment_shader_work),
		.uniform_buffer_count = 1,
	};
	gpu_shader_t* shader = gpu_shader_create(gpu, &shader_info);
	fs_work_destroy(fragment_shader_work);
	fs_work_destroy(vertex_shader_work);

	gpu_pipeline_info_t pipeline_info =
	{
		.shader = shader,
		.mesh_layout = k_gpu_mesh_layout_tri_p444_c444_i2,
	};
	gpu_pipeline_t* pipeline = gpu_pipeline_create(gpu, &pipeline_info);

	vec3f_t cube_verts[] =
	{
		{ -1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f,  1.0f },
		{  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f,  1.0f },
		{  1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f,  0.0f },
		{ -1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f,  0.0f },
		{ -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f,  1.0f },
		{  1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f,  1.0f },
		{ -1.0f,  1.0f, -1.0f }, { 0.0f, 0.0f,  0.0f },
	};
	uint16_t cube_indices[] =
	{
		0, 1, 2,
		2, 3, 0,
		1, 5, 6,
		6, 2, 1,
		7, 6, 5,
		5, 4, 7,
		4, 0, 3,
		3, 7, 4,
		4, 5, 1,
		1, 0, 4,
		3, 2, 6,
		6, 7, 3
	};
	gpu_mesh_info_t mesh_info =
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
	gpu_mesh_t* mesh = gpu_mesh_create(gpu, &mesh_info);

	struct
	{
		mat4f_t projection;
		mat4f_t model;
		mat4f_t view;
	} uniform_data;
	mat4f_make_perspective(&uniform_data.projection, (float)M_PI / 2.0f, 16.0f / 9.0f, 0.1f, 100.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&uniform_data.view, &eye_pos, &forward, &up);

	mat4f_make_identity(&uniform_data.model);

	gpu_uniform_buffer_info_t uniform_info =
	{
		.data = &uniform_data,
		.size = sizeof(uniform_data),
	};
	gpu_uniform_buffer_t* uniform_buffer = gpu_uniform_buffer_create(gpu, &uniform_info);
	
	gpu_descriptor_info_t descriptor_info =
	{
		.shader = shader,
		.uniform_buffers = &uniform_buffer,
		.uniform_buffer_count = 1,
	};
	gpu_descriptor_t* descriptor = gpu_descriptor_create(gpu, &descriptor_info);

	vec3f_t angles = vec3f_zero();

	// THIS IS THE MAIN LOOP!
	while (!wm_pump(window))
	{
		timer_object_update(root_time);

		int x, y;
		wm_get_mouse_move(window, &x, &y);

		uint32_t mask = wm_get_mouse_mask(window);

		uint32_t now = timer_ticks_to_ms(timer_get_ticks());
		debug_print(
			k_print_info,
			"T=%dms, MOUSE mask=%x move=%dx%d\n",
			timer_object_get_ms(root_time),
			mask,
			x, y);

		gpu_cmd_buffer_t* cmd_buf = gpu_frame_begin(gpu);

		angles.y += 0.01f;
		quatf_t orient = quatf_from_eulers(angles);
		mat4f_make_rotation(&uniform_data.model, &orient);
		gpu_uniform_buffer_update(gpu, uniform_buffer, &uniform_data, sizeof(uniform_data));

		gpu_cmd_pipeline_bind(gpu, cmd_buf, pipeline);
		gpu_cmd_mesh_bind(gpu, cmd_buf, mesh);
		gpu_cmd_descriptor_bind(gpu, cmd_buf, descriptor);
		gpu_cmd_draw(gpu, cmd_buf);
		gpu_frame_end(gpu);
	}

	gpu_descriptor_destroy(gpu, descriptor);
	gpu_uniform_buffer_destroy(gpu, uniform_buffer);
	gpu_mesh_destroy(gpu, mesh);
	gpu_pipeline_destroy(gpu, pipeline);
	gpu_shader_destroy(gpu, shader);

	timer_object_destroy(root_time);
	gpu_destroy(gpu);
	wm_destroy(window);
	fs_destroy(fs);
	heap_destroy(heap);

	return 0;
}

static void homework3_slower_function(trace_t* trace)
{
	trace_duration_push(trace, "homework3_slower_function");
	thread_sleep(200);
	trace_duration_pop(trace);
}

static void homework3_slow_function(trace_t* trace)
{
	trace_duration_push(trace, "homework3_slow_function");
	thread_sleep(100);
	homework3_slower_function(trace);
	trace_duration_pop(trace);
}

static int homework3_test_func(void* data)
{
	trace_t* trace = data;
	homework3_slow_function(trace);
	return 0;
}

static void homework3_test()
{
	heap_t* heap = heap_create(4096);
<<<<<<< HEAD
	void* block1 = homework1_allocate_1(heap);
	/*leaked*/ homework1_allocate_2(heap);
	/*leaked*/ homework1_allocate_3(heap);
	heap_free(heap, block1);
	heap_destroy(heap);
}

static void homework2_test_internal(heap_t* heap, fs_t* fs, bool use_compression)
{
	const char* huck_finn = "YOU don't know about me without you have read a book by the name of The Adventures of Tom Sawyer; but that ain't no matter.  That book was made by Mr. Mark Twain, and he told the truth, mainly.  There was things which he stretched, but mainly he told the truth.  That is nothing.  I never seen anybody but lied one time or another, without it was Aunt Polly, or the widow, or maybe Mary.  Aunt Polly—Tom's Aunt Polly, she is—and Mary, and the Widow Douglas is all told about in that book, which is mostly a true book, with some stretchers, as I said before. Now the way that the book winds up is this:  Tom and me found the money that the robbers hid in the cave, and it made us rich.  We got six thousand dollars apiece—all gold.  It was an awful sight of money when it was piled up.  Well, Judge Thatcher he took it and put it out at interest, and it fetched us a dollar a day apiece all the year round—more than a body could tell what to do with.  The Widow Douglas she took me for her son, and allowed she would sivilize me; but it was rough living in the house all the time, considering how dismal regular and decent the widow was in all her ways; and so when I couldn't stand it no longer I lit out.  I got into my old rags and my sugar-hogshead again, and was free and satisfied.  But Tom Sawyer he hunted me up and said he was going to start a band of robbers, and I might join if I would go back to the widow and be respectable.  So I went back. The widow she cried over me, and called me a poor lost lamb, and she called me a lot of other names, too, but she never meant no harm by it. She put me in them new clothes again, and I couldn't do nothing but sweat and sweat, and feel all cramped up.  Well, then, the old thing commenced again.  The widow rung a bell for supper, and you had to come to time. When you got to the table you couldn't go right to eating, but you had to wait for the widow to tuck down her head and grumble a little over the victuals, though there warn't really anything the matter with them,—that is, nothing only everything was cooked by itself.  In a barrel of odds and ends it is different; things get mixed up, and the juice kind of swaps around, and the things go better. After supper she got out her book and learned me about Moses and the Bulrushers, and I was in a sweat to find out all about him; but by and by she let it out that Moses had been dead a considerable long time; so then I didn't care no more about him, because I don't take no stock in dead people. Pretty soon I wanted to smoke, and asked the widow to let me.  But she wouldn't.  She said it was a mean practice and wasn't clean, and I must try to not do it any more.  That is just the way with some people.  They get down on a thing when they don't know nothing about it.  Here she was a-bothering about Moses, which was no kin to her, and no use to anybody, being gone, you see, yet finding a power of fault with me for doing a thing that had some good in it.  And she took snuff, too; of course that was all right, because she done it herself. Her sister, Miss Watson, a tolerable slim old maid, with goggles on, had just come to live with her, and took a set at me now with a spelling-book. She worked me middling hard for about an hour, and then the widow made her ease up.  I couldn't stood it much longer.  Then for an hour it was deadly dull, and I was fidgety.  Miss Watson would say, \"Don't put your feet up there, Huckleberry;\" and \"Don't scrunch up like that, Huckleberry—set up straight; \" and pretty soon she would say, \"Don't gap and stretch like that, Huckleberry—why don't you try to behave ? \"  Then she told me all about the bad place, and I said I wished I was there. She got mad then, but I didn't mean no harm.  All I wanted was to go somewheres; all I wanted was a change, I warn't particular.  She said it was wicked to say what I said; said she wouldn't say it for the whole world; she was going to live so as to go to the good place.  Well, I couldn't see no advantage in going where she was going, so I made up my mind I wouldn't try for it.  But I never said so, because it would only make trouble, and wouldn't do no good. Now she had got a start, and she went on and told me all about the good place.  She said all a body would have to do there was to go around all day long with a harp and sing, forever and ever.  So I didn't think much of it. But I never said so.  I asked her if she reckoned Tom Sawyer would go there, and she said not by a considerable sight.  I was glad about that, because I wanted him and me to be together. Miss Watson she kept pecking at me, and it got tiresome and lonesome.  By and by they fetched the niggers in and had prayers, and then everybody was off to bed.  I went up to my room with a piece of candle, and put it on the table.  Then I set down in a chair by the window and tried to think of something cheerful, but it warn't no use.  I felt so lonesome I most wished I was dead.  The stars were shining, and the leaves rustled in the woods ever so mournful; and I heard an owl, away off, who-whooing about somebody that was dead, and a whippowill and a dog crying about somebody that was going to die; and the wind was trying to whisper something to me, and I couldn't make out what it was, and so it made the cold shivers run over me. Then away out in the woods I heard that kind of a sound that a ghost makes when it wants to tell about something that's on its mind and can't make itself understood, and so can't rest easy in its grave, and has to go about that way every night grieving.  I got so down-hearted and scared I did wish I had some company.  Pretty soon a spider went crawling up my shoulder, and I flipped it off and it lit in the candle; and before I could budge it was all shriveled up.  I didn't need anybody to tell me that that was an awful bad sign and would fetch me some bad luck, so I was scared and most shook the clothes off of me. I got up and turned around in my tracks three times and crossed my breast every time; and then I tied up a little lock of my hair with a thread to keep witches away.  But I hadn't no confidence.  You do that when you've lost a horseshoe that you've found, instead of nailing it up over the door, but I hadn't ever heard anybody say it was any way to keep off bad luck when you'd killed a spider. I set down again, a-shaking all over, and got out my pipe for a smoke; for the house was all as still as death now, and so the widow wouldn't know. Well, after a long time I heard the clock away off in the town go boom—boom—boom—twelve licks; and all still again—stiller than ever. Pretty soon I heard a twig snap down in the dark amongst the trees—something was a stirring.  I set still and listened.  Directly I could just barely hear a \"me - yow!me - yow!\" down there.  That was good!  Says I, \"me - yow!me - yow!\" as soft as I could, and then I put out the light and scrambled out of the window on to the shed.  Then I slipped down to the ground and crawled in among the trees, and, sure enough, there was Tom Sawyer waiting for me.";
	const size_t huck_finn_len = strlen(huck_finn);

	const char* write_data = huck_finn;
	fs_work_t* write_work = fs_write(fs, "foo.bar", write_data, huck_finn_len, use_compression);
	fs_work_wait(write_work);

	fs_work_t* read_work = fs_read(fs, "foo.bar", heap, true, use_compression);

	assert(fs_work_get_result(write_work) == 0);
	assert(fs_work_get_size(write_work) == huck_finn_len);

	char* read_data = fs_work_get_buffer(read_work);
	assert(read_data && strcmp(read_data, huck_finn) == 0);
	assert(fs_work_get_result(read_work) == 0);
	assert(fs_work_get_size(read_work) == huck_finn_len);

	fs_work_destroy(read_work);
	fs_work_destroy(write_work);

	heap_free(heap, read_data);
}

static void homework2_test()
{
	heap_t* heap = heap_create(4096);
	fs_t* fs = fs_create(heap, 16);

	const bool disable_compression = false;
	homework2_test_internal(heap, fs, disable_compression);

	// HOMEWORK 2: Set enable_compression to true when implemented!
	//const bool enable_compression = true;
	//homework2_test_internal(heap, fs, enable_compression);

	fs_destroy(fs);
=======

	// Create the tracing system with at least space for 100 *captured* events.
	// Each call to trace_duration_push is an event.
	// Each call to trace_duration_pop is an event.
	// Before trace_capture_start is called, and after trace_capture_stop is called,
	// duration events should not be generated.
	trace_t* trace = trace_create(heap, 100);

	// Capturing has *not* started so these calls can safely be ignored.
	trace_duration_push(trace, "should be ignored");
	trace_duration_pop(trace);

	// Start capturing events.
	// Eventually we will want to write events to a file -- "trace.json".
	// However we should *not* write to the file for each call to trace_duration_push or
	// trace_duration_pop. That would be much too slow. Instead, events should be buffered
	// (up to event_capacity) before writing to a file. For purposes of this homework,
	// it is entirely fine if you only capture the first event_capacity count events and
	// ignore any additional events.
	trace_capture_start(trace, "trace.json");

	// Create a thread that will push/pop duration events.
	thread_t* thread = thread_create(homework3_test_func, trace);

	// Call a function that will push/pop duration events.
	homework3_slow_function(trace);

	// Wait for thread to finish.
	thread_destroy(thread);

	// Finish capturing. Write the trace.json file in Chrome tracing format.
	trace_capture_stop(trace);

	trace_destroy(trace);

>>>>>>> upstream/main
	heap_destroy(heap);
}
