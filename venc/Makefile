# Rockchip sample Makefile

ifeq ($(AVS_ENABLE),y)
all:
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS)  sample_multi_vi_avs_osd_venc.c -o sample_multi_vi_avs_osd_venc $(LD_FLAGS)
endif
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS)  sample_vi_vpss_osd_venc.c -o sample_vi_vpss_osd_venc $(LD_FLAGS)
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS)  sample_vi_dis_vpss_venc.c -o sample_vi_dis_vpss_venc $(LD_FLAGS)

install:
ifeq ($(AVS_ENABLE),y)
	@mv -f sample_multi_vi_avs_osd_venc $(SAMPLE_OUT_DIR)/bin
endif
	@mv -f sample_vi_vpss_osd_venc $(SAMPLE_OUT_DIR)/bin
	@mv -f sample_vi_dis_vpss_venc $(SAMPLE_OUT_DIR)/bin

clean:
	@echo "clean venc"

