#include "include/rfc3986.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char rfc3986[256] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,45,46,0,
        48,49,50,51,52,53,54,55,56,57,0,0,0,0,0,0,
        0,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,
        80,81,82,83,84,85,86,87,88,89,90,0,0,0,0,95,
        0,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,
        112,113,114,115,116,117,118,119,120,121,122,0,0,0,126,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

void rfc3986_percent_encode(const char * s, char * encoded_buffer){

	for (; *s; s++) {
		if (rfc3986[(int)*s]){
			sprintf(encoded_buffer, "%c", rfc3986[(int)*s]);
		}else{
			sprintf(encoded_buffer, "%%%02X", *s);
		}
		while (*++encoded_buffer);
	}
}

bool rfc3986_is_percent_encode_compliant(const char * s){

	for(; *s; s++)
		if(rfc3986[(int)*s] == 0 && !(*s == '%' && isxdigit(*(s+1)) && isupper(*(s+1))))
			return false;

	return true;
}

bool is_valid_sub_delims(const char * s, const char ** sub_delims_end){
	if(*s =='!' || *s == '$' || *s == '&' || *s == '\'' || *s == '(' || *s == ')' || *s == '*' || *s == '+' || *s == ',' || *s == ';' || *s == '='){
		*sub_delims_end = s+1;
		return true;
	}

	return false;
}

bool is_valid_gen_delims(const char * s, const char ** gen_delims_end){
	if(*s == ':' || *s == '/' || *s == '?' || *s == '#' || *s == '[' || *s == ']' || *s == '@'){
		*gen_delims_end = s+1;
		return true;
	}

	return false;
}

bool is_valid_reserved(const char * s, const char ** reserved_end){

	if(is_valid_gen_delims(s, reserved_end) || is_valid_sub_delims(s, reserved_end)){
		return true;
	}

	return false;
}

bool is_valid_unreserved(const char * s, const char ** unreserved_end){

	if(isalnum(*s) || *s == '-' || *s == '.' || *s == '_' || *s == '~'){
		*unreserved_end = s+1;
		return true;
	}

	return false;
}

bool is_valid_pct_encoded(const char * s, const char ** pct_encoded_end){
	if(*s == '%' && isxdigit(s[1]) && isxdigit(s[2])){
		*pct_encoded_end = s+3;
		return true;
	}

	return false;
}

bool is_valid_pchar(const char * s, const char ** pchar_end){
	if(is_valid_unreserved(s, pchar_end) || is_valid_pct_encoded(s, pchar_end) || is_valid_sub_delims(s, pchar_end)){
		return true;
	}else if(*s == ':' || *s == '@'){
		*pchar_end = s+1;
		return true;
	}

	return false;
}


bool is_valid_fragment_or_query(const char * s, const char ** fragment_end){ // Fragment and query has the same syntax
	size_t offset = 0;
	const char * tmp = NULL;
	while(*(s+offset)){
		if(is_valid_pchar(s+offset, &tmp) || *(s+offset) == '/' || *(s+offset) == '?'){
			offset++;
		}else{
			break;
		}
	}

	*fragment_end = s + offset;

	return true;
}

bool is_valid_segment_nz_nc(const char * s, const char ** segment_nz_nc_end){
	size_t offset = 0;
	const char * tmp = NULL;

	while(*(s+offset)){
		if(is_valid_unreserved(s+offset, &tmp) || is_valid_pct_encoded(s+offset, &tmp) || is_valid_sub_delims(s+offset, &tmp) || *(s+offset) == '@'){
			offset++;
		}else{
			break;
		}
	}

	*segment_nz_nc_end = s+offset;

	return offset > 0;
}

bool is_valid_segment_nz(const char * s, const char ** segment_nz_end){
	size_t offset = 0;
	const char * tmp = NULL;

	while(*(s+offset)){
		if(is_valid_pchar(s+offset, &tmp)){
			offset++;
		}else{
			break;
		}
	}

	*segment_nz_end = s+offset;

	return offset > 0;
}

bool is_valid_segment(const char * s, const char ** segment_end){
	size_t offset = 0;
	const char * tmp = NULL;

	while(*(s+offset)){
		if(is_valid_pchar(s+offset, &tmp)){
			offset++;
		}else{
			break;
		}
	}

	*segment_end = s+offset;

	return true;
}

bool is_valid_path_empty(const char * s, const char ** path_empty_end){
	if(*s == '\0' || !is_valid_pchar(s, path_empty_end)){
		*path_empty_end = s+0;
		return true;
	}else{
		return false;
	}
}

bool is_valid_path_rootless(const char * s, const char ** path_rootless_end){
	if(!is_valid_segment_nz(s, path_rootless_end)){
		return false;
	}

	while(**path_rootless_end == '/'){
		(*path_rootless_end)++;
		if(!is_valid_segment(*path_rootless_end, path_rootless_end))
			return false;
	}

	return true;
}

bool is_valid_path_noscheme(const char * s, const char ** path_noscheme_end){
	if(!is_valid_segment_nz_nc(s, path_noscheme_end)){
		return false;
	}

	while(**path_noscheme_end == '/'){
		(*path_noscheme_end)++;
		if(!is_valid_segment(*path_noscheme_end, path_noscheme_end))
			return false;
	}

	return true;
}

bool is_valid_path_absolute(const char * s, const char ** path_absolute_end){
	if(*s != '/'){
		return false;
	}

	*path_absolute_end = s+1;

	if(is_valid_segment_nz(*path_absolute_end, path_absolute_end)){
		while(**path_absolute_end == '/'){
			(*path_absolute_end)++;
			if(!is_valid_segment(*path_absolute_end, path_absolute_end))
				return false;
		}
	}

	return true;
}

bool is_valid_path_abempty(const char * s, const char ** path_abempty_end){
	*path_abempty_end = s;

	const char * tmp = NULL;
	while(**path_abempty_end == '/' && is_valid_segment((*path_abempty_end)+1, &tmp))
		*path_abempty_end = tmp;

	return true;
}

bool is_valid_path(const char * s, const char ** path_end){
	if(is_valid_path_abempty(s, path_end)
		|| is_valid_path_absolute(s, path_end)
		|| is_valid_path_noscheme(s, path_end)
		|| is_valid_path_rootless(s, path_end)
		|| is_valid_path_empty(s, path_end)){

		return true;
	}else{
		return false;
	}
}

bool is_valid_reg_name(const char * s, const char ** reg_name_end){
	*reg_name_end = s+0;

	const char * tmp = NULL;
	while(is_valid_unreserved(*reg_name_end, &tmp) || is_valid_pct_encoded(*reg_name_end, &tmp) || is_valid_sub_delims(*reg_name_end, &tmp)){
		*reg_name_end = tmp;
	}

	return true;
}

bool is_valid_dec_octet(const char * s, const char ** dec_octet_end){
	int value = 0;
	int tmp = 0;
	bool valid = false;

	for(size_t i = 0; i < 3; i++){
		if(isdigit(s[i])){
			tmp = value * 10 + (s[i] - '0');
			if(tmp > 255)
				break;
		} else {
			break;
		}

		value = tmp;
		*dec_octet_end = s+i;
		valid = true;
	}

	return valid;
}

bool is_valid_ipv4address(const char * s, const char ** ipv4address_end){

	if(!is_valid_dec_octet(s, ipv4address_end)){
		return false;
	}

	for(size_t i = 0; i < 3; i++){
		if(**ipv4address_end != '.')
			return false;

		(*ipv4address_end)++;

		if(!is_valid_dec_octet(*ipv4address_end, ipv4address_end))
			return false;
	}

	return true;
}

bool is_valid_h16(const char * s, const char ** h16_end){
	size_t count = 0;
	for(size_t i = 0; i < 4; i++){
		if(isxdigit(s[i])){
			count++;
		}else{
			break;
		}
	}

	*h16_end = s+count;
	return count > 0;
}

bool is_valid_ls32(const char * s, const char ** ls32_end){
	if(is_valid_h16(s, ls32_end) &&  **ls32_end == ':' && is_valid_h16((*ls32_end)+1, ls32_end))
		return true;

	if(is_valid_ipv4address(s, ls32_end))
		return true;

	return false;
}

bool is_valid_ipv6address(const char * s, const char ** ipv6address_end){

	size_t count = 0; // Count of (16 ":") found in current half
	const char * tmp = NULL;

	*ipv6address_end = s;

	for(size_t i = 0; i < 6; i++){

		if(is_valid_h16(*ipv6address_end, &tmp) && *tmp == ':'){
			count++;
			*ipv6address_end = tmp+1;
		}else{
			break;
		}
	}

	if(count == 6 && is_valid_ls32(*ipv6address_end, &tmp)){
		*ipv6address_end = tmp;
		return true;

	}else if(is_valid_h16(*ipv6address_end, &tmp) && tmp[0] == ':' && tmp[1] == ':'){
		*ipv6address_end = tmp+2;
		count++;

	}else if(*ipv6address_end[0] == ':'){
		(*ipv6address_end)++;

	}else{
		return false;
	}

	int max = 5 - count;// max (16 ":") in second section. Negative numbers affect ls32 (least significant 32 bit)
	count = 0;

	for(int i = 0; i < max; i++){

		if(is_valid_h16(*ipv6address_end, &tmp) && *tmp == ':'){
			count++;
			*ipv6address_end = tmp+1;
		}else{
			return false;
		}
	}

	if(max == -2){
		return true;

	}if(max == -1){
		return is_valid_h16(*ipv6address_end, ipv6address_end);
	}else{
		return is_valid_ls32(*ipv6address_end, ipv6address_end);
	}

	return false;
}

bool is_valid_ipvfuture(const char * s, const char ** ipvfuture_end){

	if(s[0] != 'v'
		&& !isxdigit(s[1])
		&& s[2] != '.'
		&& (is_valid_unreserved(s+3, ipvfuture_end) || is_valid_sub_delims(s+3, ipvfuture_end) || s[3] == ':')){

		*ipvfuture_end = s+4;
		return true;
	}else{
		return false;
	}
}

bool is_valid_ip_literal(const char * s, const char ** ip_literal_end){
	if(*s != '[')
		return false;

	if(is_valid_ipv6address(s+1, ip_literal_end) || is_valid_ipvfuture(s, ip_literal_end)){
		if(**ip_literal_end == ']'){
			(*ip_literal_end)++;
			return true;
		}
	}

	return false;
}

bool is_valid_port(const char * s, const char ** port_end){

	size_t offset_end = 0;
	while(isdigit(s[offset_end]))
		offset_end++;

	*port_end = s+offset_end;
	return true;
}

bool is_valid_host(const char * s, const char ** host_end){
	return is_valid_ip_literal(s, host_end) || is_valid_ipv4address(s, host_end) || is_valid_reg_name(s, host_end);
}

bool is_valid_userinfo(const char * s, const char ** userinfo_end){
	*userinfo_end = s+0;

	const char * tmp = NULL;

	while(is_valid_unreserved(*userinfo_end, &tmp)
		|| is_valid_pct_encoded(*userinfo_end, &tmp)
		|| is_valid_sub_delims(*userinfo_end, &tmp)
		|| **userinfo_end == ':'){

		if(tmp != NULL){
			*userinfo_end = tmp;
			tmp = NULL;
		}else{
			(*userinfo_end)++;
		}
	}

	return true;
}

bool is_valid_authority(const char * s, const char ** authority_end){
	const char * tmp = NULL;
	if(is_valid_userinfo(s, &tmp) && *tmp == '@'){
		*authority_end = tmp+1;
	}else{
		*authority_end = s+0;
	}

	if(!is_valid_host(*authority_end, authority_end))
		return false;

	if(**authority_end == ':' && is_valid_port((*authority_end)+1, &tmp))
		*authority_end = tmp;

	return true;
}

bool is_valid_scheme(const char * s, const char ** scheme_end){

	if(!isalpha(*s))
		return false;

	*scheme_end = s+1;

	while(isalpha(**scheme_end) || isdigit(**scheme_end) || **scheme_end == '+' || **scheme_end == '-' || **scheme_end == '.')
		(*scheme_end)++;

	return true;
}

bool is_valid_relative_part(const char * s, const char ** relative_part_end){
	if(s[0] != '/' || s[1] != '/')
		return false;

	if((is_valid_authority(s+2, relative_part_end) && is_valid_path_abempty(*relative_part_end, relative_part_end))
		|| is_valid_path_absolute(s+2, relative_part_end)
		|| is_valid_path_noscheme(s+2, relative_part_end)
		|| is_valid_path_empty(s+2, relative_part_end)){

		return true;
	}else{
		return false;
	}
}

bool is_valid_relative_ref(const char * s, const char ** relative_ref_end){

	if(!is_valid_relative_part(s, relative_ref_end))
		return false;

	if(**relative_ref_end == '?'){
		const char * tmp = *relative_ref_end+1;

		if(is_valid_fragment_or_query(tmp, &tmp))
			*relative_ref_end = tmp;
	}

	if(**relative_ref_end == '#'){
		const char * tmp = *relative_ref_end+1;

		if(is_valid_fragment_or_query(tmp, &tmp))
			*relative_ref_end = tmp;
	}

	return true;
}

bool is_valid_hier_part(const char * s, const char ** hier_part_end){
	const char * tmp = NULL;

	if((s[0] == '/' && s[1] == '/' && is_valid_authority(s+2, &tmp) && is_valid_path_abempty(tmp, &tmp))
		|| is_valid_path_absolute(s, &tmp)
		|| is_valid_path_rootless(s, &tmp)
		|| is_valid_path_empty(s, &tmp)){

		*hier_part_end = tmp;
		return true;
	}

	return false;
}

bool is_valid_absolute_uri(const char * s, const char ** absolute_uri_end){
	if(is_valid_scheme(s, absolute_uri_end) && **absolute_uri_end == ':' && is_valid_hier_part((*absolute_uri_end)+1, absolute_uri_end)){
		if(**absolute_uri_end == '?'){
			const char * tmp = *absolute_uri_end+1;

			if(is_valid_fragment_or_query(tmp, &tmp))
				*absolute_uri_end = tmp;
		}

		return true;
	}

	return false;
}

bool rfc3986_is_valid_uri(const char * s, const char ** uri_end){
	if(is_valid_scheme(s, uri_end) && **uri_end == ':' && is_valid_hier_part((*uri_end)+1, uri_end)){
		if(**uri_end == '?'){
			const char * tmp = *uri_end+1;

			if(is_valid_fragment_or_query(tmp, &tmp))
				*uri_end = tmp;
		}

		if(**uri_end == '#'){
			const char * tmp = (*uri_end)+1;

			if(is_valid_fragment_or_query(tmp, &tmp))
				*uri_end = tmp;
		}

		return true;
	}

	return false;
}

bool rfc3986_is_valid_uri_reference(const char * s, const char ** uri_reference_end){
	return (rfc3986_is_valid_uri(s, uri_reference_end) || is_valid_relative_ref(s, uri_reference_end));
}
