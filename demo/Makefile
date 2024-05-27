# Rockchip sample Makefile


all:
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS)  sample_direct_get_send_frame.c -o sample_direct_get_send_frame $(LD_FLAGS)
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS)  sample_multi_venc_media.c -o sample_multi_venc_media $(LD_FLAGS)

install:
	@mv -f sample_direct_get_send_frame $(SAMPLE_OUT_DIR)/bin
	@mv -f sample_multi_venc_media $(SAMPLE_OUT_DIR)/bin

clean:
	@echo "clean demo"
