#ifndef OCPP_CALL_H
#define OCPP_CALL_H

cJSON *runCall(const char* action, cJSON *payload);
void freeOcppReply();

#endif /* OCPP_CALL_H */
