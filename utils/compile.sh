#!/bin/bash -x
if [ -f .topmsg ]; then
	./.title.sh
else
	git log --oneline --no-decorate -1 HEAD
fi

echo | scripts/config --disable DRM --disable PCCARD --disable ATA --disable MD --disable PPS --disable SOUND --disable USB --disable IOMMU_SUPPORT --disable INPUT_LEDS --disable AGP --disable VGA_ARB --disable EFI --disable WLAN --disable WIRELESS --disable LOGO --disable NFS_FS --disable XFRM_USER --disable INET6_AH --disable INET6_ESP --disable NETDEVICES
echo | ./scripts/config -e MPTCP -e MPTCP_IPV6 -m MPTCP_KUNIT_TESTS
#echo | ./scripts/config --disable MPTCP || true

if [ ${#} -gt 0 ]; then
	echo | ./scripts/config "${@}"
fi

make -j$(nproc)
