#include "tests/generated/operation_collisions.h"

int main(void)
{
    SapWitOpsAlphaResource alpha = 1;
    SapWitOpsBetaResource beta = 2;
    SapWitOpsPollableResource pollable = 3;
    SapWitOpsCollisionsCommand command = {0};
    SapWitOpsCollisionsReply reply = {0};

    command.case_tag = SAP_WIT_OPS_COLLISIONS_COMMAND_ALPHA_GET;
    command.val.alpha_get.alpha = alpha;
    command.case_tag = SAP_WIT_OPS_COLLISIONS_COMMAND_BETA_SUBSCRIBE;
    command.val.beta_subscribe.beta = beta;

    reply.case_tag = SAP_WIT_OPS_COLLISIONS_REPLY_ALPHA;
    reply.val.alpha = alpha;
    reply.case_tag = SAP_WIT_OPS_COLLISIONS_REPLY_BETA;
    reply.val.beta = beta;
    reply.case_tag = SAP_WIT_OPS_COLLISIONS_REPLY_POLLABLE;
    reply.val.pollable = pollable;
    reply.case_tag = SAP_WIT_OPS_COLLISIONS_REPLY_ALPHA_GET;
    reply.case_tag = SAP_WIT_OPS_COLLISIONS_REPLY_BETA_GET;

    (void)command;
    (void)reply;
    return 0;
}
