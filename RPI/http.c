#include "net_fix.h"
#include "ssl_fix.h"
#include "http_fix.h"

#include <orbis/NpCommon.h>
#include <orbis/NpUtility.h>

#define HTTP_HEAP_SIZE (1024 * 1024)
#define SSL_HEAP_SIZE (128 * 1024)

#define DOWNLOAD_CHUNK_SIZE (16384)

#define USER_AGENT "Download/1.00"

struct download_file_cb_args {
	uint8_t* data;
	uint64_t data_size;
	uint64_t actual_size;
	uint64_t content_length;
	uint8_t* chunk;
	size_t chunk_size;
	bool is_partial;
	int status_code;
};

static int s_libssl_ctx_id = -1;
static int s_libhttp_ctx_id = -1;

static bool s_http_initialized = false;

typedef int request_cb_t(void* arg, int req_id, int status_code, uint64_t content_length, int content_length_type);

static int download_file_cb(void* arg, int req_id, int status_code, uint64_t content_length, int content_length_type);

static int do_request(const char* url, int method, const void* data, size_t data_size, const char** headers, size_t header_count, request_cb_t* cb, void* arg);

static inline bool is_good_status(int status_code);

bool http_init(void) 
{
	int ret;

	if (s_http_initialized) {
		goto done;
	}

	if (!net_is_initialized()) {
		goto err;
	}

	ret = sceSslInit(SSL_HEAP_SIZE);
	if (ret < 0) {
		KernelPrintOut("sceSslInit failed: 0x%08X\n", ret);
		goto err;
	}
	s_libssl_ctx_id = ret;

	ret = sceHttpInit(net_get_mem_id(), s_libssl_ctx_id, HTTP_HEAP_SIZE);
	if (ret < 0) {
		KernelPrintOut("sceHttpInit failed: 0x%08X\n", ret);
		goto err;
	}
	s_libhttp_ctx_id = ret;

	s_http_initialized = true;

done:
	return true;

err_http_terminate:
	ret = sceHttpTerm(s_libhttp_ctx_id);
	if (ret) {
		KernelPrintOut("sceHttpTerm failed: 0x%08X\n", ret);
	}
	s_libhttp_ctx_id = -1;

err_ssl_terminate:
	ret = sceSslTerm(s_libssl_ctx_id);
	if (ret) {
		KernelPrintOut("sceSslTerm failed: 0x%08X\n", ret);
	}
	s_libssl_ctx_id = -1;

err:
	return false;
}

void http_fini(void) {
	int ret;

	if (!s_http_initialized) {
		return;
	}

	ret = sceHttpTerm(s_libhttp_ctx_id);
	if (ret) {
		KernelPrintOut("sceHttpTerm failed: 0x%08X\n", ret);
	}
	s_libhttp_ctx_id = -1;

	ret = sceSslTerm(s_libssl_ctx_id);
	if (ret) {
		KernelPrintOut("sceSslTerm failed: 0x%08X\n", ret);
	}
	s_libssl_ctx_id = -1;

	s_http_initialized = false;
}

bool http_get_file_size(const char* url, uint64_t* total_size) {
	struct download_file_cb_args args;
	bool status = false;
	int ret;

	if (!s_http_initialized) {
		goto err;
	}
	if (!url) {
		goto err;
	}

	memset(&args, 0, sizeof(args));
	{
		args.data_size = 0;
	}

							
	ret = do_request(url, ORBIS_METHOD_GET, NULL, 0, NULL, 0, &download_file_cb, &args);
	if (ret) {
		goto err;
	}
	if (!is_good_status(args.status_code)) {
		goto err;
	}

	if (total_size) {
		*total_size = args.content_length;
	}

	status = true;

err:
	return status;
}

bool http_download_file(const char* url, uint8_t** data, uint64_t* data_size, uint64_t* total_size, uint64_t offset) {
	struct download_file_cb_args args;
	uint8_t chunk[DOWNLOAD_CHUNK_SIZE];
	const char* headers[8 * 2];
	size_t header_count = 0;
	char range_str[48];
	bool status = false;
	int ret;

	if (!s_http_initialized) {
		goto err;
	}
	if (!url) {
		goto err;
	}

	memset(&args, 0, sizeof(args));
	{
		args.data_size = data_size ? *data_size : (uint64_t)-1;
		args.chunk = chunk;
		args.chunk_size = sizeof(chunk);
	}

	memset(headers, 0, sizeof(headers));
	{
		headers[header_count * 2 + 0] = "Accept-Encoding";
		headers[header_count * 2 + 1] = "identity";
		++header_count;
	}

	if (offset > 0 && args.data_size > 0) {
		snprintf(range_str, sizeof(range_str), "bytes=%" PRIu64 "-%" PRIu64, offset, offset + args.data_size - 1);
		headers[header_count * 2 + 0] = "Range";
		headers[header_count * 2 + 1] = range_str;
		++header_count;
	}

	ret = do_request(url, ORBIS_METHOD_GET, NULL, 0, headers, header_count, &download_file_cb, &args);
	if (ret) {
		goto err_data_free;
	}
	if (!is_good_status(args.status_code)) {
		goto err;
	}

	if (data) {
		*data = args.data;
		args.data = NULL;
	}
	if (data_size) {
		*data_size = args.actual_size;
	}
	if (total_size) {
		*total_size = args.content_length;
	}

	status = true;

err_data_free:
	if (args.data) {
		free(args.data);
	}

err:
	return status;
}

bool http_escape_uri(char** out, size_t* out_size, const char* in) {
	char* tmp = NULL;
	size_t tmp_size;
	bool status = false;
	int ret;

	if (!s_http_initialized) {
		goto err;
	}

	if (!in) {
		goto err;
	}

	ret = sceHttpUriEscape(NULL, &tmp_size, 0, in);
	if (ret) {
		KernelPrintOut("sceHttpUriEscape failed: 0x%08X\n", ret);
		goto err;
	}

	tmp = (char*)malloc(tmp_size);
	if (!tmp) {
		KernelPrintOut("malloc failed\n");
		goto err;
	}
	memset(tmp, 0, tmp_size);

	ret = sceHttpUriEscape(tmp, out_size, tmp_size, in);
	if (ret) {
		KernelPrintOut("sceHttpUriEscape failed: 0x%08X\n", ret);
		goto err;
	}

	if (out) {
		*out = tmp;
		tmp = NULL;
	}

	status = true;

err:
	if (tmp) {
		free(tmp);
	}

	return status;
}

bool http_unescape_uri(char** out, size_t* out_size, const char* in) {
	char* tmp = NULL;
	size_t tmp_size;
	bool status = false;
	int ret;

	if (!s_http_initialized) {
		goto err;
	}

	if (!in) {
		goto err;
	}

	ret = sceHttpUriUnescape(NULL, &tmp_size, 0, in);
	if (ret) {
		KernelPrintOut("sceHttpUriUnescape failed: 0x%08X\n", ret);
		goto err;
	}

	tmp = (char*)malloc(tmp_size);
	if (!tmp) {
		KernelPrintOut("malloc failed\n");
		goto err;
	}
	memset(tmp, 0, tmp_size);

	ret = sceHttpUriUnescape(tmp, out_size, tmp_size, in);
	if (ret) {
		KernelPrintOut("sceHttpUriUnescape failed: 0x%08X\n", ret);
		goto err;
	}

	if (out) {
		*out = tmp;
		tmp = NULL;
	}

	status = true;

err:
	if (tmp) {
		free(tmp);
	}

	return status;
}

bool http_escape_json_string(char* out, size_t max_out_size, const char* in) {
	bool status = false;
	int ret;

	if (!s_http_initialized) {
		goto err;
	}

	if (!in) {
		goto err;
	}

	memset(out, 0, max_out_size);

	ret = sceNpUtilJsonEscape(out, max_out_size, in, strlen(in));
	if (ret) {
		KernelPrintOut("sceNpUtilJsonEscape failed: 0x%08X\n", ret);
		goto err;
	}

	status = true;

err:
	return status;
}

static int download_file_cb(void* arg, int req_id, int status_code, uint64_t content_length, int content_length_type) {
	struct download_file_cb_args* args = (struct download_file_cb_args*)arg;
	uint8_t* chunk;
	size_t chunk_size;
	uint8_t* cur_data;
	uint64_t cur_size = 0;
	uint64_t total_size;
	int ret;

	assert(args != NULL);

	args->status_code = status_code;

	if (req_id < 0) {
		ret = SCE_HTTP_ERROR_INVALID_ID;
		goto err;
	}

	if (!is_good_status(status_code)) {
		ret = 404;
		goto err;
	}

	if (content_length_type != ORBIS_HTTP_CONTENTLEN_EXIST) {
		content_length = UINT64_MAX;
	}

	if (args->data_size == (uint64_t)-1) {
		/* XXX: if Content-Length is not specified then user must specify it by himself */
		if (content_length_type != ORBIS_HTTP_CONTENTLEN_EXIST) {
			ret = SCE_HTTP_ERROR_NO_CONTENT_LENGTH;
			goto err;
		}
		total_size = content_length;
	} else if (args->data_size > content_length) {
		total_size = content_length;
	} else {
		total_size = args->data_size;
	}
	if (total_size > 0 && !args->chunk) {
		ret = SCE_HTTP_ERROR_INVALID_VALUE;
		goto err;
	}

	cur_data = args->data = (uint8_t*)malloc(total_size + 1); /* XXX: allocate one more byte to have valid cstrings */
	if (!cur_data) {
		ret = SCE_HTTP_ERROR_OUT_OF_MEMORY;
		goto err_partial_xfer;
	}
	memset(cur_data, 0, total_size + 1);

	for (chunk = args->chunk; cur_size < total_size; ) {
		chunk_size = total_size - cur_size;
		if (chunk_size > args->chunk_size) {
			chunk_size = args->chunk_size;
		}
		ret = sceHttpReadData(req_id, chunk, chunk_size);
		if (ret < 0) {
			KernelPrintOut("sceHttpReadData failed: 0x%08X\n", ret);
			goto err_partial_xfer;
		} else if (ret == 0){
			break;
		}

		memcpy(cur_data, chunk, ret);

		cur_data += ret;
		cur_size += ret;
	}

	ret = 0;

err_partial_xfer:
	args->data_size = total_size;
	args->actual_size = cur_size;
	args->content_length = content_length;

err:
	return ret;
}

static int do_request(const char* url, int method, const void* data, size_t data_size, const char** headers, size_t header_count, request_cb_t* cb, void* arg) {
	int tpl_id = -1, conn_id = -1, req_id = -1;
	unsigned int ssl_flags;
	int status_code, content_length_type;
	uint64_t content_length;
	size_t i;
	int ret;

	if (!url) {
		ret = SCE_HTTP_ERROR_INVALID_VALUE;
		goto err;
	}
	if (!headers) {
		header_count = 0;
	}

	ret = sceHttpCreateTemplate(s_libhttp_ctx_id, USER_AGENT, ORBIS_HTTP_VERSION_1_1, 1);
	if (ret < 0) {
		KernelPrintOut("sceHttpCreateTemplate failed: 0x%08X\n", ret);
		goto err;
	}
	tpl_id = ret;

	ret = sceHttpCreateConnectionWithURL(tpl_id, url, 1);
	if (ret < 0) {
		KernelPrintOut("sceHttpCreateConnectionWithURL failed: 0x%08X\n", ret);
		goto err_tpl_delete;
	}
	conn_id = ret;

	ret = sceHttpCreateRequestWithURL(conn_id, method, url, data ? data_size : 0);
	if (ret < 0) {
		KernelPrintOut("sceHttpCreateRequestWithURL failed: 0x%08X\n", ret);
		goto err_conn_delete;
	}
	req_id = ret;

	ssl_flags = SCE_HTTPS_FLAG_SERVER_VERIFY | SCE_HTTPS_FLAG_CLIENT_VERIFY;
	ssl_flags |= SCE_HTTPS_FLAG_CN_CHECK | SCE_HTTPS_FLAG_KNOWN_CA_CHECK;
	ssl_flags |= SCE_HTTPS_FLAG_NOT_AFTER_CHECK | SCE_HTTPS_FLAG_NOT_BEFORE_CHECK;

	ret = sceHttpsDisableOption(tpl_id, ssl_flags);
	if (ret) {
#if 0 /* TODO: figure out */
		KernelPrintOut("sceHttpsDisableOption failed: 0x%08X\n", ret);
		goto err;
#endif
	}

	for (i = 0; i < header_count; ++i) {
		ret = sceHttpAddRequestHeader(req_id, headers[i * 2 + 0], headers[i * 2 + 1], SCE_HTTP_HEADER_OVERWRITE);
		if (ret) {
			KernelPrintOut("sceHttpAddRequestHeader failed: 0x%08X\n", ret);
			goto err_req_delete;
		}
	}

	ret = sceHttpSendRequest(req_id, data, data ? data_size : 0);
	if (ret) {
		KernelPrintOut("sceHttpSendRequest failed: 0x%08X\n", ret);
		goto err_req_delete;
	}

	ret = sceHttpGetStatusCode(req_id, &status_code);
	if (ret < 0) {
		KernelPrintOut("sceHttpGetStatusCode failed: 0x%08X\n", ret);
		goto err_req_delete;
	}

	if (is_good_status(status_code)) {
		ret = sceHttpGetResponseContentLength(req_id, &content_length_type, &content_length);
		if (ret) {
			KernelPrintOut("sceHttpGetResponseContentLength failed: 0x%08X\n", ret);
			goto err_req_delete;
		}
	}
	if (cb) {
		ret = (*cb)(arg, req_id, status_code, content_length, content_length_type);
		if (ret) {
			goto err_req_delete;
		}
	}

err_req_delete:
	ret = sceHttpDeleteRequest(req_id);
	if (ret) {
		KernelPrintOut("sceHttpDeleteRequest failed: 0x%08X\n", ret);
	}

err_conn_delete:
	ret = sceHttpDeleteConnection(conn_id);
	if (ret) {
		KernelPrintOut("sceHttpDeleteConnection failed: 0x%08X\n", ret);
	}

err_tpl_delete:
	ret = sceHttpDeleteTemplate(tpl_id);
	if (ret) {
		KernelPrintOut("sceHttpDeleteTemplate failed: 0x%08X\n", ret);
	}

err:
	return ret;
}

static inline bool is_good_status(int status_code) {
	return (status_code == 200 || status_code == 206);
}
