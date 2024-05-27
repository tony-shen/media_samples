CURRENT_DIR := $(shell pwd)
RK_MEDIA_CHIP := rk3588
RK_MEDIA_CROSS := /data/shentao/linux/rk3588/rk3588_linux5.10_sdk_release/buildroot/output/rockchip_rk3588/host/bin/aarch64-buildroot-linux-gnu
RK_MEDIA_OUTPUT := $(CURRENT_DIR)

export CONFIG_RK_SAMPLE=y
export LC_ALL=C
SHELL:=/bin/bash

SAMPLE_CC := $(RK_MEDIA_CROSS)-gcc
SAMPLE_AR := $(RK_MEDIA_CROSS)-ar

PKG_NAME := sample
PKG_BIN ?= out
PKG_BUILD ?= build

RK_MEDIA_OPTS += -Wl,-rpath-link,${RK_MEDIA_OUTPUT}/lib:$(RK_MEDIA_OUTPUT)/root/usr/lib
PKG_CONF_OPTS += -DRKPLATFORM=ON

# debug: build cmake with more message
# PKG_CONF_OPTS += -DCMAKE_VERBOSE_MAKEFILE=ON
#
ifeq ($(RK_MEDIA_CHIP), rv1126)
PKG_CONF_OPTS += -DCMAKE_SYSTEM_PROCESSOR=armv7l
PKG_CONF_OPTS += -DARCH64=OFF
endif

ifeq ($(RK_MEDIA_CHIP), rk3588)
export AVS_ENABLE=y
PKG_CONF_OPTS += -DARCH64=ON -DAVS_ENABLE=y
endif

ifeq ($(RK_MEDIA_CHIP), rv1106)
PKG_CONF_OPTS += -DARCH64=OFF
endif

ifeq ($(CONFIG_RK_SAMPLE),y)
PKG_TARGET := sample-build
else
PKG_TARGET :=
$(warning Not config source RK_SAMPLE, Skip...)
endif

ifeq ($(PKG_BIN),)
$(error ### $(CURRENT_DIR): PKG_BIN is NULL, Please Check !!!)
endif

COMM_DIR := $(CURRENT_DIR)/common
COMM_SRC := $(wildcard $(COMM_DIR)/*.c)
ifeq ($(RK_MEDIA_CHIP), rv1126)
COMM_SRC += $(wildcard $(COMM_DIR)/isp2.x/*.c)
endif
ifeq ($(RK_MEDIA_CHIP), rk3588)
COMM_SRC += $(wildcard $(COMM_DIR)/isp3.x/*.c)
endif
ifeq ($(RK_MEDIA_CHIP), rv1106)
COMM_SRC += $(wildcard $(COMM_DIR)/isp3.x/*.c)
endif
COMM_OBJ := $(COMM_SRC:%.c=%.o)

INC_FLAGS := -I$(COMM_DIR)
INC_FLAGS += -I$(CURRENT_DIR)/../rockit/mpi/sdk/include
INC_FLAGS += -I$(CURRENT_DIR)/../rockit/lib/lib64
INC_FLAGS += -I$(CURRENT_DIR)/../camera_engine_rkaiq/rkaiq/include
INC_FLAGS += -I$(CURRENT_DIR)/../camera_engine_rkaiq/rkaiq/include/uAPI2
INC_FLAGS += -I$(CURRENT_DIR)/../camera_engine_rkaiq/rkaiq/include/common
INC_FLAGS += -I$(CURRENT_DIR)/../camera_engine_rkaiq/rkaiq/include/xcore
INC_FLAGS += -I$(CURRENT_DIR)/../camera_engine_rkaiq/rkaiq/include/algos
INC_FLAGS += -I$(CURRENT_DIR)/../camera_engine_rkaiq/rkaiq/include/iq_parser
INC_FLAGS += -I$(CURRENT_DIR)/../camera_engine_rkaiq/rkaiq/include/iq_parser_v2
SAMPLE_CFLAGS += -g -Wall $(INC_FLAGS) $(PKG_CONF_OPTS) -lpthread -lm -ldl
LD_FLAGS += $(RK_MEDIA_OPTS) -L$(RK_MEDIA_OUTPUT)/lib  -lrockit -lrockchip_mpp -lrkaiq -L$(SAMPLE_OUT_DIR) -lsample_comm

ifeq ($(RK_MEDIA_CHIP), rv1126)
INC_FLAGS += -I$(COMM_DIR)/isp2.x
SAMPLE_CFLAGS += -DISP_HW_V20
LD_FLAGS += -L$(CURRENT_DIR)/lib  -lrtsp_32bit
LD_FLAGS += -L$(RK_MEDIA_OUTPUT)/root/usr/lib -lasound
SAMPLE_CFLAGS += -DHAVE_VO
LD_FLAGS += -ldrm
endif

ifeq ($(RK_MEDIA_CHIP), rk3588)
INC_FLAGS += -I$(COMM_DIR)/isp3.x
SAMPLE_CFLAGS += -DISP_HW_V30
LD_FLAGS += -L$(CURRENT_DIR)/lib  -lrtsp_64bit
LD_FLAGS += -L$(RK_MEDIA_OUTPUT)/root/usr/lib -lasound
SAMPLE_CFLAGS += -DHAVE_VO
LD_FLAGS += -ldrm
endif

ifeq ($(RK_MEDIA_CHIP), rv1106)
INC_FLAGS += -I$(COMM_DIR)/isp3.x
SAMPLE_CFLAGS += -DISP_HW_V30
LD_FLAGS += -L$(CURRENT_DIR)/lib  -lrtsp_uclibc
endif

export SAMPLE_OUT_DIR=$(CURRENT_DIR)/out
export PKG_CONF_OPTS
export COMM_OBJ COMM_SRC COMM_DIR
export SAMPLE_CC SAMPLE_AR
export SAMPLE_CFLAGS
export LD_FLAGS

$(info "SAMPLE_CFLAGS $(SAMPLE_CFLAGS)")
$(info "LD_FLAGS $(LD_FLAGS)")

all: $(PKG_TARGET)
	@echo "build $(PKG_NAME) done";

sample-build: libasound librkaiq librockit COMM_LIB
	@mkdir -p $(SAMPLE_OUT_DIR)/bin
	@make -C $(CURRENT_DIR)/audio;
	@make -C $(CURRENT_DIR)/audio install;
	@make -C $(CURRENT_DIR)/vi;
	@make -C $(CURRENT_DIR)/vi install;
ifneq ($(RK_MEDIA_CHIP), rv1106)
	@make -C $(CURRENT_DIR)/vo;
	@make -C $(CURRENT_DIR)/vo install;
endif
	@make -C $(CURRENT_DIR)/venc;
	@make -C $(CURRENT_DIR)/venc install;
ifeq ($(RK_MEDIA_CHIP), rk3588)
	@make -C $(CURRENT_DIR)/avs;
	@make -C $(CURRENT_DIR)/avs install;
	@make -C $(CURRENT_DIR)/demo;
	@make -C $(CURRENT_DIR)/demo install;
endif
	@make -C $(CURRENT_DIR)/test;
	@make -C $(CURRENT_DIR)/test install;
	@cp -rfa $(SAMPLE_OUT_DIR)/* $(RK_MEDIA_OUTPUT)

COMM_LIB:
	@make -C $(CURRENT_DIR)/common

libasound:
	@test ! -d $(RK_MEDIA_TOP_DIR)/alsa-lib || make -C $(RK_MEDIA_TOP_DIR)/alsa-lib

librkaiq:
	@test ! -d $(RK_MEDIA_TOP_DIR)/isp || make -C $(RK_MEDIA_TOP_DIR)/isp

librockit:
	@test ! -d $(RK_MEDIA_TOP_DIR)/rockit || make -C $(RK_MEDIA_TOP_DIR)/rockit

clean:
	@make -C $(CURRENT_DIR)/common clean
	@make -C $(CURRENT_DIR)/audio clean
	@make -C $(CURRENT_DIR)/vi clean
	@make -C $(CURRENT_DIR)/vo clean
	@make -C $(CURRENT_DIR)/venc clean
ifeq ($(RK_MEDIA_CHIP), rk3588)
	@make -C $(CURRENT_DIR)/avs clean
	@make -C $(CURRENT_DIR)/demo clean
endif
	@make -C $(CURRENT_DIR)/test clean
	@rm -rf $(SAMPLE_OUT_DIR)

distclean: clean
