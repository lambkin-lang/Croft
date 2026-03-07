(module
  (import "env" "host_log" (func $host_log (param i32 i32 i32)))

  (memory (export "memory") 1)

  (func (export "text_metric") (param $ptr i32) (param $len i32) (result i32)
    (call $host_log
      (i32.const 1)
      (local.get $ptr)
      (local.get $len)
    )

    (i32.add
      (local.get $len)
      (i32.const 42)
    )
  )
)
