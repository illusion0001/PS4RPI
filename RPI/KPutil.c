#include <orbis/libkernel.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void Notify(const char* FMT, ...)
{
	OrbisNotificationRequest Buffer;

	va_list args;
	va_start(args, FMT);
	vsprintf(Buffer.message, FMT, args);
	va_end(args);

	Buffer.type = NotificationRequest;
	Buffer.unk3 = 0;
	Buffer.useIconImageUri = 1;
	Buffer.targetId = -1;
	strcpy(Buffer.iconUri, "cxml://psnotification/tex_icon_system");

	sceKernelSendNotificationRequest(0, &Buffer, 3120, 0);
}

void KernelPrintOut(const char* FMT, ...)
{
	char MessageBuf[1024];
	va_list args;
	va_start(args, FMT);
	vsprintf(MessageBuf, FMT, args);
	va_end(args);

	sceKernelDebugOutText(0, MessageBuf);
}

void SafeExit(const char* reason, ...)
{
	char MessageBuf[1024];
	va_list args;
	va_start(args, reason);
	vsprintf(MessageBuf, reason, args);
	va_end(args);

	KernelPrintOut(reason);
	sceSystemServiceLoadExec((char*)"exit", NULL);
}
