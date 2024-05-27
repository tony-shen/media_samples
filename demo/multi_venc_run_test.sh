#!/bin/sh

# encode type:  h264cbr h264vbr h265cbr h265vbr mjpegcbr mjpegvbr
code_type=h264cbr

#set test loop
test_loop=10000

#set frame count for every loop
frame_count=10


#h265cbr
test_case()
{
    # demo/sample_multi_venc_media

    #PN switch test
    echo -e "--------------------------------------- <sample_multi_venc_media> PN switch test start -------------------------------------------\n"
    sample_multi_venc_media --avsWidth 8192 --avsHeight 2700 -a /etc/iqfiles/ -e $code_type -n 6 -b 4096 --moduleTestType 1 --moduleTestLoop $test_loop --testFrameCount $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_multi_venc_media>  PN switch test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_multi_venc_media> PN switch test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    # avs dpi switch
    echo -e "--------------------------------------- <sample_multi_venc_media> avs dpi switch test start -------------------------------------------\n"
    sample_multi_venc_media --avsWidth 8192 --avsHeight 2700 -a /etc/iqfiles/ -e $code_type -n 6 -b 4096 --moduleTestType 2 --moduleTestLoop $test_loop --testFrameCount $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_multi_venc_media> avs dpi switch test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_multi_venc_media> avs dpi switch test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    #sub stream dpi switch
    echo -e "--------------------------------------- <sample_multi_venc_media> sub stream dpi switch test start -------------------------------------------\n"
    sample_multi_venc_media --avsWidth 8192 --avsHeight 2700 -a /etc/iqfiles/ -e $code_type -n 6 -b 4096 --moduleTestType 3 --moduleTestLoop $test_loop --testFrameCount $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_multi_venc_media> sub stream dpi switch test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_multi_venc_media> sub stream dpi switch test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    #venc encode type switch
    echo -e "--------------------------------------- <sample_multi_venc_media> venc encode type switch test start -------------------------------------------\n"
    sample_multi_venc_media --avsWidth 8192 --avsHeight 2700 -a /etc/iqfiles/ -e $code_type -n 6 -b 4096 --moduleTestType 4 --moduleTestLoop $test_loop --testFrameCount $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_multi_venc_media> venc encode type switch test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_multi_venc_media> venc encode type switch test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    #bind and ubind(afbc/none) switch
    echo -e "--------------------------------------- <sample_multi_venc_media> bind and ubind(afbc/none) switch test start -------------------------------------------\n"
    sample_multi_venc_media --avsWidth 8192 --avsHeight 2700 -a /etc/iqfiles/ -e $code_type -n 6 -b 4096 --moduleTestType 5 --moduleTestLoop $test_loop --testFrameCount $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_multi_venc_media> bind and ubind test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_multi_venc_media> bind and ubind test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    #avs_vpss resolution switch test
    echo -e "--------------------------------------- <sample_multi_venc_media> avs_vpss resolution switch test start -------------------------------------------\n"
    sample_multi_venc_media --avsWidth 8192 --avsHeight 2700 -a /etc/iqfiles/ -e $code_type -n 6 -b 4096 --moduleTestType 6 --moduleTestLoop $test_loop --testFrameCount $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_multi_venc_media> avs_vpss resolution switch test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_multi_venc_media> avs_vpss resolution switch test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    #main stream dpi switch
    echo -e "--------------------------------------- <sample_multi_venc_media> main stream dpi switch test start -------------------------------------------\n"
    sample_multi_venc_media --avsWidth 8192 --avsHeight 2700 -a /etc/iqfiles/ -e $code_type -n 6 -b 4096 --moduleTestType 7 --moduleTestLoop $test_loop --testFrameCount $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_multi_venc_media> main stream dpi switch test_result success -------------------------------------------\n\n\n"
    else
        echo -e "--------------------------------------- <sample_multi_venc_media> main stream dpi switch test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    #rgn attach and detach switch
    echo -e "--------------------------------------- <sample_multi_venc_media> rgn attach and detach switch test start -------------------------------------------\n"
    sample_multi_venc_media --avsWidth 8192 --avsHeight 2700 -a /etc/iqfiles/ -e $code_type -n 6 -b 4096 --moduleTestType 8 --moduleTestLoop $test_loop --testFrameCount $frame_count
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_multi_venc_media> rgn destroy and create test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_multi_venc_media> rgn destroy and create test_result failure -------------------------------------------\n\n\n"
    fi
    sleep 3

    #ordinary stream
    echo -e "--------------------------------------- <sample_multi_venc_media> ordinary stream test start -------------------------------------------\n"
    sample_multi_venc_media --avsWidth 8192 --avsHeight 2700 -a /etc/iqfiles/ -e $code_type -n 6 -b 4096 --moduleTestType 0 -l -1
    if [ $? -eq 0 ]; then
        echo -e "--------------------------------------- <sample_multi_venc_media> ordinary stream test_result success -------------------------------------------\n\n\n"
    else 
        echo -e "--------------------------------------- <sample_multi_venc_media> ordinary stream test_result failure -------------------------------------------\n\n\n"
    fi

    echo -e "--------------------------------------- ALL Test End -------------------------------------------\n\n"
}


test_case



