CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -pthread
INC=-I../bgm -I../h264 -I../nr3d -I../integration -I../jpeg -I../mux -I../audio -I../transport -I../timing -I../observability
TESTS=test_bgm_adapter test_bgm_stock_route test_bgm_linux_wire test_h264_control test_nr3d_kernel test_nr3d_lifecycle test_ghosting_policy test_lifecycle_pass_a test_lifecycle_pass_b test_h264_app_rc test_h264_stream test_jpeg_stream test_jpeg_config test_jpeg_lifecycle test_mp4_sample test_mp4_generation test_h264_txn test_audio_fanout test_flv_generation test_rtmp_session test_timestamp test_bitrate_observer test_http_client

all: $(TESTS)

test_bgm_adapter: test_bgm_adapter.c ../bgm/fh8626_bgm_adapter.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_bgm_stock_route: test_bgm_stock_route.c ../bgm/fh8626_bgm_stock_route.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_bgm_linux_wire: test_bgm_linux_wire.c ../bgm/fh8626_bgm_linux.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_h264_control: test_h264_control.c ../h264/fh8626_h264_control.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_ghosting_policy: test_ghosting_policy.c ../integration/fh8626_ghosting_policy.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_nr3d_lifecycle: test_nr3d_lifecycle.c ../nr3d/fh8626_nr3d_lifecycle.c ../nr3d/fh8626_nr3d_kernel.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_nr3d_kernel: test_nr3d_kernel.c ../nr3d/fh8626_nr3d_kernel.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_lifecycle_pass_a: test_lifecycle_pass_a.c ../bgm/fh8626_bgm_adapter.c ../bgm/fh8626_bgm_stock_route.c ../h264/fh8626_h264_control.c ../nr3d/fh8626_nr3d_kernel.c ../nr3d/fh8626_nr3d_lifecycle.c ../integration/fh8626_ghosting_policy.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_lifecycle_pass_b: test_lifecycle_pass_b.c ../bgm/fh8626_bgm_adapter.c ../h264/fh8626_h264_control.c ../h264/fh8626_h264_txn.c ../nr3d/fh8626_nr3d_kernel.c ../nr3d/fh8626_nr3d_lifecycle.c ../integration/fh8626_ghosting_policy.c ../audio/fh8626_audio_fanout.c ../integration/fh8626_flv_generation.c ../transport/fh8626_rtmp_session.c ../timing/fh8626_timestamp.c ../observability/fh8626_bitrate_observer.c ../transport/fh8626_http_client.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_h264_app_rc: test_h264_app_rc.c ../h264/fh8626_h264_app_rc.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_h264_stream: test_h264_stream.c ../h264/fh8626_h264_stream.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_jpeg_stream: test_jpeg_stream.c ../jpeg/fh8626_jpeg_stream.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_jpeg_config: test_jpeg_config.c ../jpeg/fh8626_jpeg_config.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_jpeg_lifecycle: test_jpeg_lifecycle.c ../jpeg/fh8626_jpeg_lifecycle.c ../jpeg/fh8626_jpeg_owned_image.c ../jpeg/fh8626_jpeg_stream.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_mp4_sample: test_mp4_sample.c ../mux/fh8626_mp4_sample.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_mp4_generation: test_mp4_generation.c ../integration/fh8626_mp4_generation.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_h264_txn: test_h264_txn.c ../h264/fh8626_h264_txn.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_audio_fanout: test_audio_fanout.c ../audio/fh8626_audio_fanout.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_flv_generation: test_flv_generation.c ../integration/fh8626_flv_generation.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_rtmp_session: test_rtmp_session.c ../transport/fh8626_rtmp_session.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_timestamp: test_timestamp.c ../timing/fh8626_timestamp.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_bitrate_observer: test_bitrate_observer.c ../observability/fh8626_bitrate_observer.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@
test_http_client: test_http_client.c ../transport/fh8626_http_client.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@

gates: run_host_gates.sh
	./run_host_gates.sh

test: check
check: all
	@set -e; for t in $(TESTS); do ./$$t; done
clean:
	rm -f $(TESTS)

.PHONY: all test check gates clean
