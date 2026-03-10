#include "tests/generated/use_reexports_types.h"

int main(void)
{
    SapWitReexportsDescriptorStat stat = {0};
    SapWitReexportsErrorResource error = 1;
    SapWitReexportsOutputStreamResource output_stream = 2;
    SapWitReexportsTypesCommand command = {0};
    SapWitReexportsTypesReply reply = {0};

    stat.has_type = 1;
    stat.has_last_error = 1;
    stat.last_error = error;

    command.case_tag = SAP_WIT_REEXPORTS_TYPES_COMMAND_DRAIN;
    command.val.drain.stream = output_stream;

    reply.case_tag = SAP_WIT_REEXPORTS_TYPES_REPLY_DESCRIPTOR_STAT;
    reply.val.descriptor_stat = stat;
    reply.case_tag = SAP_WIT_REEXPORTS_TYPES_REPLY_INPUT_STREAM;
    reply.case_tag = SAP_WIT_REEXPORTS_TYPES_REPLY_STATUS;

    (void)command;
    (void)reply;
    return 0;
}
