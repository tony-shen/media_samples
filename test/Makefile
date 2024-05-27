# Rockchip sample Makefile

SRCS := $(wildcard *.c)
OBJS  := $(SRCS:%.c=%.o)
TARGET := $(SRCS:%.c=%)

.PHONY : clean all

TARGET1 = sample_camera_stresstest
TARGET2 = sample_multi_camera_stresstest

ifeq ($(RK_MEDIA_CHIP), rk3588)
MULLTI_CAMERA_ENABLE = y
endif


ifeq ($(MULLTI_CAMERA_ENABLE),y)
all: 
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS) sample_multi_camera_stresstest.c -o sample_multi_camera_stresstest $(LD_FLAGS)
else
all:
	@$(SAMPLE_CC) $(SAMPLE_CFLAGS) sample_camera_stresstest.c -o sample_camera_stresstest $(LD_FLAGS)
endif

install:
ifeq ($(MULLTI_CAMERA_ENABLE),y)
	@mv -f sample_multi_camera_stresstest $(SAMPLE_OUT_DIR)/bin
else
	@mv -f sample_camera_stresstest $(SAMPLE_OUT_DIR)/bin
endif

clean:
	@echo "clean test"
