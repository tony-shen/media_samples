# media_samples
## ���
media_sample�ǵ���media�������һЩʾ������rockit��isp��iva�ȡ�samples��ipc sdk�����Դ���ʾ����������rk3588_linux_sdk_releaseͨ��linuxƽ̨��samplesʾ����û����ֲ������Ϊ�˷�����ͨ��linuxƽ̨��Ҳ����ͨsamplesʾ��������������һ���򵥵���ֲ��

## Demo����˵��
���»���rk3588_linux5.10_sdk_release����������media_samples��
 - ��BR2_PACKAGE_ROCKIT������rockit
   1. cd rk3588_linux5.10_sdk_release
   2. ./build.sh lunch
   3. buildroot/envsetup.sh rockchip_rk3588
   4. cd buildroot
   5. make menuconfig
   6. make rockit
   ```sh
   Symbol: BR2_PACKAGE_ROCKIT [=n]
   Type  : bool
   Prompt: rockit
   Location:                                              
    -> Target packages                                   
      -> Hardware Platforms                              
        -> Rockchip Platform (BR2_PACKAGE_ROCKCHIP [=y]) 
          -> Rockchip BSP packages
            -> rockit [Y]
   ```
   ע�⣺������rockit����Ҫִ��./build.sh rootfs��Ȼ��������дrootfs.img�̼���ϵͳ���Զ�����rockit���ļ���Ҳ�����ֶ�push���ļ���ϵͳ��

 - ����media_samples
   1. ��media_samples������rk3588_linux5.10_sdk_release/externalĿ¼��
   2. ��������lib�⵽media_samples/libĿ¼��
      ```sh
      cp ../../buildroot/output/rockchip_rk3588/target/usr/lib/librockit.so lib/
      cp ../../buildroot/output/rockchip_rk3588/target/usr/lib/librockchip_mpp.so lib/
      cp ../../buildroot/output/rockchip_rk3588/target/usr/lib/librkaiq.so lib/
      cp ../../buildroot/output/rockchip_rk3588/target/usr/lib/libasound.so lib/
      cp ../../buildroot/output/rockchip_rk3588/target/usr/lib/libdrm.so lib/
      ```
   3. �޸�Makefile�е�RK_MEDIA_CROSS���������·��
   4. ��media_samplesĿ¼��ִ��make������samplesʾ��
   ע�⣺����samples voʾ��������Ҫ��librkAlgoDis.so��push���豸�У� ��adb push librkAlgoDis.so /usr/lib64��
         ���⣬ͨ��linuxƽ̨���Զ�����rkaiq_3A_server������samples֮ǰ����ִ��/etc/init.d/S40rkaiq_3A stop����Ϊsamplesʾ�������-a����Ҳ������rkaiq_3A_server��
      