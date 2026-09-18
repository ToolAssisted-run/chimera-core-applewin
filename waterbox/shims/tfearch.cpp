/* The Uthernet card's packet capture: there is no network in the sandbox, so
 * no adapter is ever found and the card sees a cable with nothing on it. */
#include "StdAfx.h"
#include "Tfe/tfearch.h"

bool tfe_arch_enumadapter_open() { return false; }
bool tfe_arch_enumadapter(std::string &, std::string &) { return false; }
bool tfe_arch_enumadapter_close() { return false; }
pcap_t *TfePcapOpenAdapter(const std::string &) { return nullptr; }
void TfePcapCloseAdapter(pcap_t *) {}
void tfe_arch_set_mac(const BYTE[6]) {}
void tfe_arch_set_hashfilter(const uint32_t[2]) {}
void tfe_arch_recv_ctl(bool, bool, bool, bool, bool, bool) {}
void tfe_arch_line_ctl(bool, bool) {}
void tfe_arch_transmit(pcap_t *, int, BYTE *) {}
int tfe_arch_receive(pcap_t *, const int, BYTE *) { return -1; }
bool tfe_arch_is_npcap_loaded() { return false; }
const char *tfe_arch_lib_version() { return "none"; }
