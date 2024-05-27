# Rockchip sample Makefile
TARGET1 = sample_multi_vi_avs
TARGET2 = sample_avs

all:
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS) sample_multi_vi_avs.c  -o sample_multi_vi_avs $(LD_FLAGS)
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS) sample_avs.c           -o sample_avs          $(LD_FLAGS)

install:
	@mv -f sample_multi_vi_avs $(SAMPLE_OUT_DIR)/bin
	@mv -f sample_avs $(SAMPLE_OUT_DIR)/bin
clean:
	@echo "clean avs"

