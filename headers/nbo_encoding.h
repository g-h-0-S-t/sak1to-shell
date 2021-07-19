uint64_t htonll(uint64_t int_data) {
	return ((1==htonl(1)) ? (int_data) : (((uint64_t)htonl((int_data) & 0xFFFFFFFFUL)) << 32) | htonl((uint32_t)((int_data) >> 32)));
}

uint64_t ntohll(uint64_t nbytes) {
	return ((1==ntohl(1)) ? (nbytes) : (((uint64_t)ntohl((nbytes) & 0xFFFFFFFFUL)) << 32) | ntohl((uint32_t)((nbytes) >> 32)));
}
