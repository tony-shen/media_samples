#!/bin/sh

# encode type:  h264cbr h264vbr h265cbr h265vbr mjpegcbr mjpegvbr
code_type=h264cbr

#set test loop
test_loop=10000

#set frame count for every loop
frame_count=10




test_case()
{
    #demo/sample_direct_get_send_frame
    #PN mode switch
    echo -e "--------------------------------------- <sample_direct_get_send_frame> PN mode switch test start -------------------------------------------\n"
    sample_direct_get_send_frame -w 8160 -h 3616 -a /etc/iqfiles/ -n 6 -e $code_type --module_test_type 1 --module_test_loop $test_loop --test_frame_count $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_direct_get_send_frame> PN mode switch test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_direct_get_send_frame> PN mode switch test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    # avs dpi switch
    echo -e "--------------------------------------- <sample_direct_get_send_frame> avs dpi switch test start -------------------------------------------\n"
    sample_direct_get_send_frame -w 8160 -h 3616 -a /etc/iqfiles/ -n 6 -e $code_type --module_test_type 2 --module_test_loop $test_loop --test_frame_count $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_direct_get_send_frame> avs dpi switch test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_direct_get_send_frame> avs dpi switch test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    #main stream dpi switch
    echo -e "--------------------------------------- <sample_direct_get_send_frame> main stream dpi switch test start -------------------------------------------\n"
    sample_direct_get_send_frame -w 8160 -h 3616 -a /etc/iqfiles/ -n 6 -e $code_type --module_test_type 3 --module_test_loop $test_loop --test_frame_count $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_direct_get_send_frame> main stream dpi switch test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_direct_get_send_frame> main stream dpi switch test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    #ordinary stream
    echo -e "--------------------------------------- <sample_direct_get_send_frame> ordinary stream test start -------------------------------------------\n"
    sample_direct_get_send_frame -w 8160 -h 3616 -a /etc/iqfiles/ -n 6 -e $code_type -l -1
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_direct_get_send_frame> ordinary stream test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_direct_get_send_frame> ordinary stream test_result failure -------------------------------------------\n\n\n"
    fi

    echo -e "--------------------------------------- ALL Test End -------------------------------------------\n\n"
}

test_case



