#pragma once

extern const char * get_proto_name(unsigned int proto);
extern void find_specific_proto(const char *protoarg);
extern void parse_exclude_protos(const char *arg);
extern unsigned int find_next_enabled_proto(unsigned int from);
