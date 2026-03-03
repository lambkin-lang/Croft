(module
  ;; Import host_log(level, ptr, len)
  (import "env" "host_log" (func $host_log (param i32 i32 i32)))

  ;; Data segment for strings in Wasm linear memory
  (memory (export "memory") 1)
  (data (i32.const 1024) "Wasm Guest saying hello from WAT!") 

  ;; Exported test_function_a
  (func (export "test_function_a") (param $arg i32) (result i32)
    ;; call host_log(CROFT_LOG_INFO=1, ptr=1024, len=33)
    (call $host_log
      (i32.const 1)
      (i32.const 1024)
      (i32.const 33)
    )

    ;; return arg + 42
    (i32.add
      (local.get $arg)
      (i32.const 42)
    )
  )

  ;; Exported wasm_handle_event
  (func (export "wasm_handle_event") (param $event_type i32) (param $arg0 i32) (param $arg1 i32) (result i32)
    (if (result i32) (i32.eq (local.get $event_type) (i32.const 100))
      (then
        (i32.const 1) ;; Return 1 (true)
      )
      (else
        (i32.const 0) ;; Return 0 (false)
      )
    )
  )
)
