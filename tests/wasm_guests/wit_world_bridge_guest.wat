(module
  (import "env" "croft_wit_find_endpoint"
    (func $croft_wit_find_endpoint (param i32 i32) (result i32)))
  (import "env" "croft_wit_call_endpoint"
    (func $croft_wit_call_endpoint (param i32 i32 i32 i32 i32 i32) (result i32)))

  (memory (export "memory") 1)

  (func (export "invoke_endpoint")
        (param $name_ptr i32)
        (param $name_len i32)
        (param $command_ptr i32)
        (param $command_len i32)
        (param $reply_ptr i32)
        (param $reply_cap i32)
        (param $reply_len_ptr i32)
        (result i32)
    (local $endpoint i32)

    (local.set $endpoint
      (call $croft_wit_find_endpoint
        (local.get $name_ptr)
        (local.get $name_len)))

    (if (i32.lt_s (local.get $endpoint) (i32.const 1))
      (then
        (return (local.get $endpoint))))

    (return
      (call $croft_wit_call_endpoint
        (local.get $endpoint)
        (local.get $command_ptr)
        (local.get $command_len)
        (local.get $reply_ptr)
        (local.get $reply_cap)
        (local.get $reply_len_ptr))))
)
