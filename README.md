# media_samples
## 简介
media_sample是调用media各个库的一些示例，如rockit，isp，iva等。samples在ipc sdk中有自带的示例。但是在rk3588_linux_sdk_release通用linux平台上samples示例并没有移植过来。为了方便在通用linux平台上也能跑通samples示例，本工程做了一个简单的移植。

## Demo编译说明
以下基于rk3588_linux5.10_sdk_release环境来编译media_samples。
 - 打开BR2_PACKAGE_ROCKIT，编译rockit
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
   注意：编译完rockit后，需要执行./build.sh rootfs，然后重新烧写rootfs.img固件，系统就自动包括rockit库文件，也可以手动push库文件到系统。

 - 编译media_samples
   1. 将media_samples拷贝到rk3588_linux5.10_sdk_release/external目录中
   2. 拷贝下面lib库到media_samples/lib目录中
      ```sh
      cp ../../buildroot/output/rockchip_rk3588/target/usr/lib/librockit.so lib/
      cp ../../buildroot/output/rockchip_rk3588/target/usr/lib/librockchip_mpp.so lib/
      cp ../../buildroot/output/rockchip_rk3588/target/usr/lib/librkaiq.so lib/
      cp ../../buildroot/output/rockchip_rk3588/target/usr/lib/libasound.so lib/
      cp ../../buildroot/output/rockchip_rk3588/target/usr/lib/libdrm.so lib/
      ```
   3. 修改Makefile中的RK_MEDIA_CROSS交叉编译链路径
   4. 在media_samples目录下执行make，编译samples示例
   注意：运行samples vo示例，还需要将librkAlgoDis.so库push到设备中， 如adb push librkAlgoDis.so /usr/lib64。
         另外，通用linux平台会自动启动rkaiq_3A_server，运行samples之前请先执行/etc/init.d/S40rkaiq_3A stop，因为samples示例添加了-a参数也会启动rkaiq_3A_server。
      