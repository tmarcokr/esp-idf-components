import pytest
from pytest_embedded import Dut

@pytest.mark.esp32c6
@pytest.mark.parametrize('count', [1])
def test_rgb_led_hardware(dut: Dut):
    """
    Automated HIL test for the RgbLed component.
    Flashes the firmware to an ESP32-C6 and runs Unity test cases.
    """
    # The object 'dut' (Device Under Test) manages the serial connection
    dut.expect_exact("Press ENTER to see the list of tests")
    dut.write("*") # Executes all Unity test cases
    
    # Wait for Unity's final summary
    dut.expect(r"OK", timeout=60) 
    dut.expect(r"0 Failures", timeout=10)
