#include "generated/wit_wasi_cli_environment.h"
#include "generated/wit_wasi_cli_stdio.h"
#include "generated/wit_wasi_cli_terminal.h"
#include "generated/wit_wasi_random_insecure.h"

int main(void)
{
    SapWitCliEnvironmentCommand env_command = {0};
    SapWitCliEnvironmentReply env_reply = {0};
    SapWitCliStdinCommand stdin_command = {0};
    SapWitCliStdinReply stdin_reply = {0};
    SapWitCliStdoutCommand stdout_command = {0};
    SapWitCliStdoutReply stdout_reply = {0};
    SapWitCliTerminalStdinCommand terminal_stdin_command = {0};
    SapWitCliTerminalStdinReply terminal_stdin_reply = {0};
    SapWitRandomInsecureCommand random_command = {0};
    SapWitRandomInsecureReply random_reply = {0};

    env_command.case_tag = SAP_WIT_CLI_ENVIRONMENT_COMMAND_GET_ARGUMENTS;
    env_reply.case_tag = SAP_WIT_CLI_ENVIRONMENT_REPLY_GET_ARGUMENTS;
    stdin_command.case_tag = SAP_WIT_CLI_STDIN_COMMAND_GET_STDIN;
    stdin_reply.case_tag = SAP_WIT_CLI_STDIN_REPLY_INPUT_STREAM;
    stdout_command.case_tag = SAP_WIT_CLI_STDOUT_COMMAND_GET_STDOUT;
    stdout_reply.case_tag = SAP_WIT_CLI_STDOUT_REPLY_OUTPUT_STREAM;
    terminal_stdin_command.case_tag = SAP_WIT_CLI_TERMINAL_STDIN_COMMAND_GET_TERMINAL_STDIN;
    terminal_stdin_reply.case_tag = SAP_WIT_CLI_TERMINAL_STDIN_REPLY_GET_TERMINAL_STDIN;
    random_command.case_tag = SAP_WIT_RANDOM_INSECURE_COMMAND_GET_INSECURE_RANDOM_U64;
    random_reply.case_tag = SAP_WIT_RANDOM_INSECURE_REPLY_GET_INSECURE_RANDOM_U64;

    if (sap_wit_cli_interfaces_count == 0u || sap_wit_random_interfaces_count == 0u) {
        return 1;
    }

    (void)env_command;
    (void)env_reply;
    (void)stdin_command;
    (void)stdin_reply;
    (void)stdout_command;
    (void)stdout_reply;
    (void)terminal_stdin_command;
    (void)terminal_stdin_reply;
    (void)random_command;
    (void)random_reply;
    return 0;
}
