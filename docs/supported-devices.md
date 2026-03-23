
# Supported Devices

Supported GPUs are defined in `drivers/gpu/cuda/devices.h` as macros using the Device ID. As explained in a [GitHub issue](https://github.com/hrushikesh-sam/GPUdirect-test-BF2/issues/1), if the Device ID / Macro is not present, gpudev will always return `0` GPU devices found.

>[!WARNING]
> The gpudev library worked as expected on my GPU after adding support. I cannot guarantee the same for you.
> While highly unlikely anything bad would happen to an unsupported GPU, I'm not responsible for any damage to
> a GPU as a result of following this guide.


## Finding GPU Device ID

Use lspci to get the device ID. On the right, you'll see the vendor and device ID with a colon delimiter. The vendor ID/Device of the device show in the example below is `10de:2331`.

The Vendor ID for NVIDIA is `10de` and the device ID is `2331`. The device ID will be set as the macro value.

```
root@nv-net-1:~/dpdk# lspci -nn | grep -i nvidia
c1:00.0 3D controller [0302]: NVIDIA Corporation GH100 [H100 PCIe] [10de:2331] (rev a1)
root@nv-net-1:~/dpdk#

```

## Necessary Changes

I decided to fork the DPDK repo to persist changes for this project. If you don't have access to a support GPU device, I recommend doing the same. There were a few other bugs, [such as a double newline error](https://github.com/mgreenj/dpdk/commit/066a73573bedd02234110b15c0b1e900f30a43c4), that I also patched.

Adding support for your GPU is simple. You only have to modify two files: drivers/gpu/cuda/grdcopy.c and drivers/gpu/cuda/cuda.c.  Please see [my commit to the forked copy of DPDK](https://github.com/DPDK/dpdk/commit/c2352487571894dc27b526fb5fdd39cd9d8b7222) for an example.