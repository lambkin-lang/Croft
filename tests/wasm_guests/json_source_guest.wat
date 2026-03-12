(module
  (import "env" "host_log" (func $host_log (param i32 i32 i32)))

  (memory (export "memory") 1)

  (data (i32.const 1024) "json_source_guest ready")
  (data (i32.const 1152)
    "{\22project\22:\22Croft\22,\22features\22:{\22solver\22:true,\22thatch\22:\22json\22},\22items\22:[1,2,3]}")

  (func (export "json_demo_announce") (result i32)
    (call $host_log
      (i32.const 1)
      (i32.const 1024)
      (i32.const 23))
    (i32.const 0))

  (func (export "json_demo_input_ptr") (result i32)
    (i32.const 1152))

  (func (export "json_demo_input_len") (result i32)
    (i32.const 78))
)
