# Rockchip sample Makefile

all:
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS) sample_vi_vo.c  -o sample_vi_vo $(LD_FLAGS)
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS) sample_vi_dis_vo.c  -o sample_vi_dis_vo $(LD_FLAGS)
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS) sample_multi_vi_dis_vo.c  -o sample_multi_vi_dis_vo $(LD_FLAGS)

install:
	@mv -f sample_vi_vo $(SAMPLE_OUT_DIR)/bin
	@mv -f sample_vi_dis_vo $(SAMPLE_OUT_DIR)/bin
	@mv -f sample_multi_vi_dis_vo $(SAMPLE_OUT_DIR)/bin
clean:
	@echo "clean vo"



