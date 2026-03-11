#include "generated/wit_wasi_clocks_monotonic_clock.h"
#include "generated/wit_wasi_clocks_timezone.h"
#include "generated/wit_wasi_io_poll.h"

int main(void)
{
    SapWitClocksDatetime when = {0};
    SapWitClocksTimezoneCommand timezone_command = {0};
    SapWitClocksTimezoneReply timezone_reply = {0};
    SapWitClocksMonotonicClockCommand monotonic_command = {0};
    SapWitClocksMonotonicClockReply monotonic_reply = {0};
    SapWitClocksPollableResource pollable = 0;
    SapWitIoPollableResource io_pollable = 0;

    when.seconds = 1;
    timezone_command.case_tag = SAP_WIT_CLOCKS_TIMEZONE_COMMAND_DISPLAY;
    timezone_command.val.display = when;
    timezone_reply.case_tag = SAP_WIT_CLOCKS_TIMEZONE_REPLY_TIMEZONE_DISPLAY;
    monotonic_command.case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_COMMAND_SUBSCRIBE_DURATION;
    monotonic_command.val.subscribe_duration.when = 1u;
    monotonic_reply.case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_REPLY_POLLABLE;
    monotonic_reply.val.pollable = pollable;

    (void)timezone_command;
    (void)timezone_reply;
    (void)monotonic_command;
    (void)monotonic_reply;
    (void)io_pollable;
    return 0;
}
