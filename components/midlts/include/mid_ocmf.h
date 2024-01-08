#ifndef __MID_OCMF_H__
#define __MID_OCMF_H__

int midocmf_create_fiscal_message(char *msg_buf, size_t msg_size,
		const char *serial, const char *app_version, const char *mid_version, time_t time, uint32_t mid_status, uint32_t energyWh);

#endif
