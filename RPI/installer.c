#include "installer.h"
#include "pkg.h"
#include "util.h"

#include <orbis/libkernel.h>
#include <orbis/UserService.h>
#include <orbis/AppInstUtil.h>
#include <orbis/Bgft.h>
#include <sys/param.h>

#define _NDBG 
#define BGFT_HEAP_SIZE (1 * 1024 * 1024)
#define WAIT_TIME (UINT64_C(5) * 1000 * 1000) /* 5 secs */

#define SCE_BGFT_INVALID_TASK_ID (-1)
#define SCE_BGFT_ERROR_SAME_APPLICATION_ALREADY_INSTALLED (0x80990088)

enum bgft_task_option_t {
	BGFT_TASK_OPTION_NONE = 0x0,
	BGFT_TASK_OPTION_DELETE_AFTER_UPLOAD = 0x1,
	BGFT_TASK_OPTION_INVISIBLE = 0x2,
	BGFT_TASK_OPTION_ENABLE_PLAYGO = 0x4,
	BGFT_TASK_OPTION_FORCE_UPDATE = 0x8,
	BGFT_TASK_OPTION_REMOTE = 0x10,
	BGFT_TASK_OPTION_COPY_CRASH_REPORT_FILES = 0x20,
	BGFT_TASK_OPTION_DISABLE_INSERT_POPUP = 0x40,
	BGFT_TASK_OPTION_DISABLE_CDN_QUERY_PARAM = 0x10000,
};

static OrbisBgftInitParams s_bgft_init_params;

static bool s_app_inst_util_initialized = false;
static bool s_bgft_initialized = false;

static bool modify_download_task_for_patch_internal(const char* path, int index);
static bool modify_download_task_for_patch(OrbisBgftTaskId task_id);

bool app_inst_util_init(void) {
	int ret;

	if (s_app_inst_util_initialized) {
		goto done;
	}

	ret = sceAppInstUtilInitialize();
	if (ret) {
		KernelPrintOut("sceAppInstUtilInitialize failed: 0x%08X\n", ret);
		goto err;
	}

	s_app_inst_util_initialized = true;

done:
	return true;

err:
	s_app_inst_util_initialized = false;

	return false;
}

void app_inst_util_fini(void) {
	int ret;

	if (!s_app_inst_util_initialized) {
		return;
	}

	ret = sceAppInstUtilTerminate();
	if (ret) {
		KernelPrintOut("sceAppInstUtilTerminate failed: 0x%08X\n", ret);
	}

	s_app_inst_util_initialized = false;
}

bool app_inst_util_uninstall_game(const char* title_id, int* error) {
	int ret;

	if (!s_app_inst_util_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (!title_id) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceAppInstUtilAppUnInstall(title_id);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceAppInstUtilAppUnInstall failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

bool app_inst_util_uninstall_ac(const char* content_id, int* error) {
	struct pkg_content_info content_info;
	int ret;

	if (!s_app_inst_util_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (!content_id) {
invalid_content_id:
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}
	if (!pkg_parse_content_id(content_id, &content_info)) {
		goto invalid_content_id;
	}

	ret = sceAppInstUtilAppUnInstallAddcont(content_info.title_id, content_info.label);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceAppInstUtilAppUnInstallAddcont failed: 0x%08X\n", ret);
		goto err;
	}

done:
	return true;

err:
	return false;
}

bool app_inst_util_uninstall_patch(const char* title_id, int* error) {
	int ret;

	if (!s_app_inst_util_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (!title_id) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceAppInstUtilAppUnInstallPat(title_id);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceAppInstUtilAppUnInstallPat failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

bool app_inst_util_uninstall_theme(const char* content_id, int* error) {
	int ret;

	if (!s_app_inst_util_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (!content_id) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceAppInstUtilAppUnInstallTheme(content_id);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceAppInstUtilAppUnInstallTheme failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

bool app_inst_util_is_exists(const char* title_id, bool* exists, int* error) {
	int flag;
	int ret;

	if (!s_app_inst_util_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (!title_id) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceAppInstUtilAppExists(title_id, &flag);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceAppInstUtilAppExists failed: 0x%08X\n", ret);
		goto err;
	}

	if (exists) {
		*exists = flag;
	}

	return true;

err:
	return false;
}

bool app_inst_util_get_size(const char* title_id, unsigned long* size, int* error) {
	int ret;

	if (!s_app_inst_util_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (!title_id) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceAppInstUtilAppGetSize(title_id, size);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceAppInstUtilAppGetSize failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

bool bgft_init(void) 
{
	int ret;

	if (s_bgft_initialized) {
		goto done;
	}

	memset(&s_bgft_init_params, 0, sizeof(s_bgft_init_params));
	{
		s_bgft_init_params.heapSize = BGFT_HEAP_SIZE;
		s_bgft_init_params.heap = (uint8_t*)malloc(s_bgft_init_params.heapSize);
		if (!s_bgft_init_params.heap) {
			KernelPrintOut("No memory for BGFT heap.\n");
			goto err;
		}
		memset(s_bgft_init_params.heap, 0, s_bgft_init_params.heapSize);
	}

	ret = sceBgftServiceIntInit(&s_bgft_init_params);
	if (ret) {
		KernelPrintOut("sceBgftServiceIntInit failed: 0x%08X\n", ret);
		goto err_bgft_heap_free;
	}

	s_bgft_initialized = true;

done:
	return true;

err_bgft_heap_free:
	if (s_bgft_init_params.heap) {
		free(s_bgft_init_params.heap);
		s_bgft_init_params.heap = NULL;
	}

	memset(&s_bgft_init_params, 0, sizeof(s_bgft_init_params));

err:
	s_bgft_initialized = false;

	return false;

}

void bgft_fini(void) {
	int ret;

	if (!s_bgft_initialized) {
		return;
	}

	ret = sceBgftServiceIntTerm();
	if (ret) {
		KernelPrintOut("sceBgftServiceIntTerm failed: 0x%08X\n", ret);
	}

	if (s_bgft_init_params.heap) {
		free(s_bgft_init_params.heap);
		s_bgft_init_params.heap = NULL;
	}

	memset(&s_bgft_init_params, 0, sizeof(s_bgft_init_params));

	s_bgft_initialized = false;
}

bool bgft_download_register_package_task(const char* content_id, const char* content_url, const char* content_name, const char* icon_path, const char* package_type, const char* package_sub_type, unsigned long package_size, bool is_patch, int* out_task_id, int* error)
{
	OrbisBgftDownloadParam params;
	OrbisBgftDownloadRegisterErrorInfo error_info;
	OrbisBgftTaskId task_id;
	struct pkg_content_info content_info;
	int user_id;
	int ret;

	if (!s_app_inst_util_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}
	if (!s_bgft_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (!pkg_parse_content_id(content_id, &content_info)) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceUserServiceGetForegroundUser(&user_id);
	if (ret) {
		KernelPrintOut("sceUserServiceGetForegroundUser failed: 0x%08X\n", ret);
		goto err;
	}

	memset(&error_info, 0, sizeof(error_info));

	memset(&params, 0, sizeof(params));
	{
		params.entitlementType = 5; /* TODO: figure out */
		params.userId = user_id;
		params.id = content_id;
		params.contentUrl = content_url;
		params.contentName = content_name;
		params.iconPath = icon_path ? icon_path : "";
		params.playgoScenarioId = "0";
		params.option = ORBIS_BGFT_TASK_OPT_DISABLE_CDN_QUERY_PARAM;
		params.packageType = package_type;
		params.packageSubType = package_sub_type ? package_sub_type : "";
		params.packageSize = package_size;
	}

	task_id = SCE_BGFT_INVALID_TASK_ID;

	if (!is_patch) {
		ret = sceBgftServiceIntDownloadRegisterTask(&params, &task_id);
	}
	else {
		ret = sceBgftServiceIntDebugDownloadRegisterPkg(&params, &task_id);
	}
	if (ret) {
		if (error) {
			*error = ret;
		}
		if (ret == SCE_BGFT_ERROR_SAME_APPLICATION_ALREADY_INSTALLED) {
			task_id = -1;
			//printf("Package already installed.\n");
			goto done;
		}
		KernelPrintOut("sceBgftServiceIntDownloadRegisterTask failed: 0x%08X\n", ret);
		goto err;
	}

done:
	if (out_task_id) {
		*out_task_id = (int)task_id;
	}

	return true;

err:
	return false;
}

bool bgft_download_start_task(int task_id, int* error) {
	int ret;

	if (!s_bgft_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (task_id < 0) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceBgftServiceDownloadStartTask((OrbisBgftTaskId)task_id);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceBgftServiceDownloadStartTask failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

bool bgft_download_stop_task(int task_id, int* error) {
	int ret;

	if (!s_bgft_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (task_id < 0) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceBgftServiceDownloadStopTask((OrbisBgftTaskId)task_id);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceBgftServiceDownloadStopTask failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

bool bgft_download_pause_task(int task_id, int* error) {
	int ret;

	if (!s_bgft_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (task_id < 0) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceBgftServiceDownloadPauseTask((OrbisBgftTaskId)task_id);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceBgftServiceDownloadPauseTask failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

bool bgft_download_resume_task(int task_id, int* error) {
	int ret;

	if (!s_bgft_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (task_id < 0) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceBgftServiceDownloadResumeTask((OrbisBgftTaskId)task_id);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceBgftServiceDownloadResumeTask failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

bool bgft_download_unregister_task(int task_id, int* error) {
	int ret;

	if (!s_bgft_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (task_id < 0) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	ret = sceBgftServiceIntDownloadUnregisterTask((OrbisBgftTaskId)task_id);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceBgftServiceIntDownloadUnregisterTask failed: 0x%08X\n", ret);
		goto err;
	}

	return true;

err:
	return false;
}

bool bgft_download_reregister_task_patch(int old_task_id, int* new_task_id, int* error) {
	OrbisBgftTaskId tmp_id;
	int ret;

	if (!s_bgft_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (old_task_id < 0) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	tmp_id = SCE_BGFT_INVALID_TASK_ID;
	ret = sceBgftServiceIntDownloadReregisterTaskPatch((OrbisBgftTaskId)old_task_id, &tmp_id);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceBgftServiceIntDownloadReregisterTaskPatch failed: 0x%08X\n", ret);
		goto err;
	}

	if (new_task_id) {
		*new_task_id = (int)tmp_id;
	}

	return true;

err:
	return false;
}

bool bgft_download_get_task_progress(int task_id, struct bgft_download_task_progress_info* progress_info, int* error) {
	OrbisBgftTaskProgress tmp_progress_info;
	int ret;

	if (!s_bgft_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (task_id < 0) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}
	if (!progress_info) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	memset(&tmp_progress_info, 0, sizeof(tmp_progress_info));
	ret = sceBgftServiceDownloadGetProgress((OrbisBgftTaskId)task_id, &tmp_progress_info);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceBgftServiceDownloadGetProgress failed: 0x%08X\n", ret);
		goto err;
	}

	memset(progress_info, 0, sizeof(*progress_info));
	{
		progress_info->bits = tmp_progress_info.bits;
		progress_info->error_result = tmp_progress_info.errorResult;
		progress_info->length = tmp_progress_info.length;
		progress_info->transferred = tmp_progress_info.transferred;
		progress_info->length_total = tmp_progress_info.lengthTotal;
		progress_info->transferred_total = tmp_progress_info.transferredTotal;
		progress_info->num_index = tmp_progress_info.numIndex;
		progress_info->num_total = tmp_progress_info.numTotal;
		progress_info->rest_sec = tmp_progress_info.restSec;
		progress_info->rest_sec_total = tmp_progress_info.restSecTotal;
		progress_info->preparing_percent = tmp_progress_info.preparingPercent;
		progress_info->local_copy_percent = tmp_progress_info.localCopyPercent;
	}

	return true;

err:
	return false;
}

bool bgft_download_find_task_by_content_id(const char* content_id, int sub_type, int* task_id, int* error) {
	OrbisBgftTaskId tmp_id;
	int ret;

	if (!s_bgft_initialized) {
		ret = ORBIS_KERNEL_ERROR_ENXIO;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	if (!content_id) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}
	if (!((OrbisBgftTaskSubType)sub_type > ORBIS_BGFT_TASK_SUB_TYPE_UNKNOWN && (OrbisBgftTaskSubType)sub_type < ORBIS_BGFT_TASK_SUB_TYPE_MAX)) {
		ret = ORBIS_KERNEL_ERROR_EINVAL;
		if (error) {
			*error = ret;
		}
		goto err;
	}

	tmp_id = SCE_BGFT_INVALID_TASK_ID;
	ret = sceBgftServiceDownloadFindTaskByContentId(content_id, (OrbisBgftTaskSubType)sub_type, &tmp_id);
	if (ret) {
		if (error) {
			*error = ret;
		}
		KernelPrintOut("sceBgftServiceDownloadFindTaskByContentId failed: 0x%08X\n", ret);
		goto err;
	}

	if (task_id) {
		*task_id = (int)tmp_id;
	}

	return true;

err:
	return false;
}
