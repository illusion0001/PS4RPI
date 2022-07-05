#include "net_fix.h"

#include <orbis/libkernel.h>
#include <orbis/NetCtl.h>

#define NET_HEAP_SIZE (1 * 1024 * 1024)

static int s_libnet_mem_id = -1;

static bool s_net_initialized = false;

bool net_init(void) {
	int ret;

	if (s_net_initialized) {
		goto done;
	}

	ret = sceNetCtlInit();
	if (ret) {
		KernelPrintOut("sceNetCtlInit failed: 0x%08X\n", ret);
		goto err;
	}

	ret = sceNetInit();
	if (ret) {
		KernelPrintOut("sceNetInit failed: 0x%08X\n", sceNetErrnoLoc);
		goto err_netctl_terminate;
	}

	ret = sceNetPoolCreate("remote_pkg_inst_net_pool", NET_HEAP_SIZE, 0);
	if (ret < 0) {
		KernelPrintOut("sceNetPoolCreate failed: 0x%08X\n", sceNetErrnoLoc);
		goto err_net_terminate;
	}
	s_libnet_mem_id = ret;

	s_net_initialized = true;

done:
	return true;

err_pool_destroy:
	ret = sceNetPoolDestroy(s_libnet_mem_id);
	if (ret < 0) {
		KernelPrintOut("sceNetPoolDestroy failed: 0x%08X\n", sceNetErrnoLoc());
	}
	s_libnet_mem_id = -1;

err_net_terminate:
	ret = sceNetTerm();
	if (ret) {
		KernelPrintOut("sceNetTerm failed: 0x%08X\n", sceNetErrnoLoc());
	}

err_netctl_terminate:
	sceNetCtlTerm();

err:
	return false;
}

bool net_is_initialized(void) {
	return s_net_initialized;
}

int net_get_mem_id(void) {
	if (!s_net_initialized) {
		return -1;
	}

	return s_libnet_mem_id;
}

void net_fini(void) {
	int ret;

	if (!s_net_initialized) {
		return;
	}

	ret = sceNetPoolDestroy(s_libnet_mem_id);
	if (ret < 0) {
		KernelPrintOut("sceNetPoolDestroy failed: 0x%08X\n", sceNetErrnoLoc());
	}
	s_libnet_mem_id = -1;

	ret = sceNetTerm();
	if (ret < 0) {
		KernelPrintOut("sceNetTerm failed: 0x%08X\n", sceNetErrnoLoc());
	}

	sceNetCtlTerm();

	s_net_initialized = false;
}

int net_get_ipv4(char* buf, size_t buf_size) {
	OrbisNetCtlInfo info;
	int ret;

	if (buf_size < sizeof(info.ip_address)) {
		ret = 0x80020016;
		goto err;
	}

	memset(&info, 0, sizeof(info));
	ret = sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info);
	if (ret) {
		KernelPrintOut("sceNetCtlGetInfo failed: 0x%08X\n", ret);
		goto err;
	}

	strncpy(buf, info.ip_address, buf_size);

err:
	return ret;
}

int net_send_all(int sock_id, const void* data, size_t size, size_t* sent) {
	uint8_t* ptr = (uint8_t*)data;
	size_t total_sent = 0;
	size_t cur_size;
	int ret;

	while (total_sent < size) {
		cur_size = size - total_sent;

		ret = sceNetSend(sock_id, ptr, cur_size, 0);
		if (ret < 0) {
			KernelPrintOut("sceNetSend failed: 0x%08X\n", sceNetErrnoLoc());
			goto err;
		}
		if (ret == 0) {
			break;
		}

		total_sent += ret;
		ptr += ret;
	}

	ret = 0;

err:
	if (sent)
		*sent = total_sent;

	return ret;
}

int net_recv_all(int sock_id, void* data, size_t size, size_t* received) {
	uint8_t* ptr = (uint8_t*)data;
	size_t total_received = 0;
	size_t cur_size;
	int ret;

	while (total_received < size) {
		cur_size = size - total_received;

		ret = sceNetRecv(sock_id, ptr, cur_size, 0);
		if (ret < 0) {
			KernelPrintOut("sceNetRecv failed: 0x%08X\n", sceNetErrnoLoc());
			goto err;
		}
		if (ret == 0) {
			break;
		}

		total_received += ret;
		ptr += ret;
	}

	ret = 0;

err:
	if (received)
		*received = total_received;

	return ret;
}
