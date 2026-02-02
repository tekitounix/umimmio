#include "test_transport.hh"
#include <cs43l22/cs43l22.hh>
#include <umitest.hh>

int main() {
    umitest::Suite s("mmio_cs43l22");

    s.section("verify_id");
    s.run("valid chip ID", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        bus(device::CS43L22::i2c_address, 0x01) = 0xE0;
        device::CS43L22Driver driver(transport);
        t.assert_true(driver.verify_id());
        return !t.failed;
    });

    s.run("invalid chip ID", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        bus(device::CS43L22::i2c_address, 0x01) = 0x00;
        device::CS43L22Driver driver(transport);
        t.assert_true(!driver.verify_id());
        return !t.failed;
    });

    s.section("init");
    s.run("init 16-bit", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        bus(device::CS43L22::i2c_address, 0x01) = 0xE3;
        device::CS43L22Driver driver(transport);
        t.assert_true(driver.init(false));
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x02), static_cast<uint8_t>(0x01));
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x04), static_cast<uint8_t>(0xAF));
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x05), static_cast<uint8_t>(0x81));
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x06), static_cast<uint8_t>(0x04));
        return !t.failed;
    });

    s.run("init 24-bit", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        bus(device::CS43L22::i2c_address, 0x01) = 0xE3;
        device::CS43L22Driver driver(transport);
        driver.init(true);
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x06), static_cast<uint8_t>(0x06));
        return !t.failed;
    });

    s.section("power");
    s.run("power_on", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        device::CS43L22Driver driver(transport);
        driver.power_on();
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x02), static_cast<uint8_t>(0x9E));
        return !t.failed;
    });

    s.run("power_off", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        device::CS43L22Driver driver(transport);
        driver.power_off();
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x02), static_cast<uint8_t>(0x01));
        return !t.failed;
    });

    s.section("volume");
    s.run("volume 0dB", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        device::CS43L22Driver driver(transport);
        driver.set_volume(0);
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x20), static_cast<uint8_t>(0x00));
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x21), static_cast<uint8_t>(0x00));
        return !t.failed;
    });

    s.run("volume +12dB", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        device::CS43L22Driver driver(transport);
        driver.set_volume(12);
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x20), static_cast<uint8_t>(0x18));
        return !t.failed;
    });

    s.run("volume -1dB", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        device::CS43L22Driver driver(transport);
        driver.set_volume(-1);
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x20), static_cast<uint8_t>(0xFE));
        return !t.failed;
    });

    s.section("mute");
    s.run("mute on", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        device::CS43L22Driver driver(transport);
        driver.mute(true);
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x04), static_cast<uint8_t>(0xFF));
        return !t.failed;
    });

    s.run("mute off", [](umitest::TestContext& t) {
        test::MockI2cBus bus;
        test::I2cTransport<test::MockI2cBus> transport(bus, device::CS43L22::i2c_address);
        device::CS43L22Driver driver(transport);
        driver.mute(false);
        t.assert_eq(bus(device::CS43L22::i2c_address, 0x04), static_cast<uint8_t>(0xAF));
        return !t.failed;
    });

    return s.summary();
}
