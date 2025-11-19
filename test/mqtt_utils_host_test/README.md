# Host test for utility functions of MQTT client

This is a host test which tests utility functions from `mqtt_utils` subcomponent. 

## Usage

To run the test suite you will need to set target to `Linux` and build the application. You will also need to enable `(Top) → Compiler options → Enable C++ run-time type info (RTTI)` (`CONFIG_COMPILER_CXX_RTTI`)
The default sdkconfig should set those options automatically, so usually just the command below will be enough to run it.

```bash
idf.py build monitor
```

## Example structure

- [main/idf_component.yml](main/idf_component.yml) adds a dependency on `espressif/catch2` component.
- [main/CMakeLists.txt](main/CMakeLists.txt) specifies the source files and registers the `main` component with `WHOLE_ARCHIVE` option enabled.
- [CMakeLists.txt](CMakeLists.txt) includes CPM package manager and adds rapidcheck package
- [main/test_main.cpp](main/test_main.cpp) implements the application entry point which calls the test runner.
- [main/test_cases.cpp](main/test_cases.cpp) implements test cases.
- [sdkconfig.defaults](sdkconfig.defaults) sets the options required to run the example: enables C++ exceptions, increases the size of the `main` task stack, and enables C++ runtime type info.

## Expected output

```
Randomness seeded to: 2196951535
Using configuration: seed=951423191499285688

- Testing the decoding of random string
OK, passed 100 tests

- Testing the decoding of random URI
OK, passed 100 tests
===============================================================================
All tests passed (403 assertions in 1 test case)

Test passed.
```
