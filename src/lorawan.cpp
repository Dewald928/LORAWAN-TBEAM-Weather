#include "lorawan.h"

// Chose LSB mode on the console and then copy it here.
// static const u1_t PROGMEM APPEUI[8] = {0x00, 0x00, 0x00, 0x50, 0x0D, 0xB3, 0x00, 0x07};
static const u1_t PROGMEM APPEUI[8] = {0x01, 0x00, 0x00, 0x50, 0x0D, 0xB3, 0x00, 0x07};

// LSB mode
// static const u1_t PROGMEM DEVEUI[8] = {0xFF, 0xFE, 0x00, 0x79, 0x24, 0xB2, 0x67, 0xAC}; // test
static const u1_t PROGMEM DEVEUI[8] = {0xF0, 0xFE, 0x00, 0x79, 0x24, 0xB2, 0x67, 0xAC}; // true

// MSB mode
// static const u1_t PROGMEM APPKEY[16] = {0xEA, 0x32, 0x04, 0xCF, 0xEE, 0x43, 0x98, 0xD4, 0x25, 0x4E, 0xEB, 0x26, 0xD6, 0xE1, 0x2E, 0x5B};
static const u1_t PROGMEM APPKEY[16] = {0xAD, 0xDB, 0x7B, 0x1E, 0xC7, 0x60, 0xDD, 0xD3, 0xE1, 0x3D, 0xCA, 0xFF, 0xD9, 0xF4, 0x3D, 0x0C};

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = RADIO_CS_PIN,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = RADIO_RST_PIN,
    .dio = {RADIO_DI0_PIN, RADIO_DIO1_PIN, RADIO_BUSY_PIN}};

static osjob_t sendjob;
static int spreadFactor = DR_SF7;
static int joinStatus = EV_JOINING;
static String lora_msg = "";
unsigned TX_INTERVAL = LORA_INTERVAL * 60;

void os_getArtEui(u1_t *buf)
{
    memcpy_P(buf, APPEUI, 8);
}

void os_getDevEui(u1_t *buf)
{
    memcpy_P(buf, DEVEUI, 8);
}

void os_getDevKey(u1_t *buf)
{
    memcpy_P(buf, APPKEY, 16);
}

void do_send(osjob_t *j)
{
    if (joinStatus == EV_JOINING)
    {
        Serial.println(F("Not joined yet"));
        // Check if there is not a current TX/RX job running
        os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
    }
    else if (LMIC.opmode & OP_TXRXPEND)
    {
        Serial.println(F("OP_TXRXPEND, not sending"));
    }
    else
    {
        Serial.println(F("OP_TXRXPEND,sending ..."));
        // Prepare upstream data transmission at the next possible time.
        take_readings();
        LMIC_setTxData2(1, lpp.getBuffer(), lpp.getSize(), 0);
        os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);

#ifdef HAS_DISPLAY
        if (u8g2)
        {
            char buf[256];
            u8g2->clearBuffer();
            snprintf(buf, sizeof(buf), "[%lu]data sending!", millis() / 1000);
            u8g2->drawStr(0, 12, buf);
            u8g2->sendBuffer();
        }
#endif
    }
}

void onEvent(ev_t ev)
{
    Serial.print(os_getTime());
    Serial.print(": ");
    switch (ev)
    {
    case EV_TXCOMPLETE:
        Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));

        if (LMIC.txrxFlags & TXRX_ACK)
        {
            Serial.println(F("Received ack"));
            lora_msg = "Received ACK.";
        }

        lora_msg = "rssi:" + String(LMIC.rssi) + " snr: " + String(LMIC.snr);

        if (LMIC.dataLen)
        {
            // data received in rx slot after tx
            Serial.print(F("Received "));
            Serial.print(LMIC.dataLen);
            Serial.print(F(" bytes of payload: 0x"));
            for (int i = 0; i < LMIC.dataLen; i++)
            {
                if (LMIC.frame[LMIC.dataBeg + i] < 0x10)
                {
                    Serial.print(F("0"));
                }
                Serial.print(LMIC.frame[LMIC.dataBeg + i], HEX);
            }
            Serial.println();
            // parsePayload();
        }
        // Schedule next transmission
        os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
        break;
    case EV_JOINING:
        Serial.println(F("EV_JOINING: -> Joining..."));
        lora_msg = "OTAA joining....";
        joinStatus = EV_JOINING;
#ifdef HAS_DISPLAY
        if (u8g2)
        {
            u8g2->clearBuffer();
            u8g2->drawStr(0, 12, "OTAA joining....");
            u8g2->sendBuffer();
        }
#endif
        break;
    case EV_JOIN_FAILED:
        Serial.println(F("EV_JOIN_FAILED: -> Joining failed"));
        lora_msg = "OTAA Joining failed";
        PMU.setChgLEDMode(AXP20X_LED_BLINK_1HZ);
#ifdef HAS_DISPLAY
        if (u8g2)
        {
            u8g2->clearBuffer();
            u8g2->drawStr(0, 12, "OTAA joining failed");
            u8g2->sendBuffer();
        }
#endif
        break;
    case EV_JOINED:
        Serial.println(F("EV_JOINED"));
        lora_msg = "Joined!";
        joinStatus = EV_JOINED;
        PMU.setChgLEDMode(AXP20X_LED_OFF);

#ifdef HAS_DISPLAY
        if (u8g2)
        {
            u8g2->clearBuffer();
            u8g2->drawStr(0, 12, "Joined TTN!");
            u8g2->sendBuffer();
        }
#endif
        delay(3);
        // Disable link check validation (automatically enabled
        // during join, but not supported by TTN at this time).
        LMIC_setLinkCheckMode(0);

        break;
    case EV_RXCOMPLETE:
        // data received in ping slot
        Serial.println(F("EV_RXCOMPLETE"));
        break;
    case EV_LINK_DEAD:
        Serial.println(F("EV_LINK_DEAD"));
        break;
    case EV_LINK_ALIVE:
        Serial.println(F("EV_LINK_ALIVE"));
        break;
    default:
        Serial.println(F("Unknown event"));
        break;
    }
}

void setupLMIC(void)
{
    Serial.println("Starting LORA...");

    // LMIC init
    os_init();

    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);
    // Set up the channels used by the Things Network, which corresponds
    // to the defaults of most gateways. Without this, only three base
    // channels from the LoRaWAN specification are used, which certainly
    // works, so it is good for debugging, but can overload those
    // frequencies, so be sure to configure the full frequency range of
    // your network here (unless your network autoconfigures them).
    // Setting up channels should happen after LMIC_setSession, as that
    // configures the minimal channel set.

    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI); // g-band
    LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK, DR_FSK), BAND_MILLI);   // g2-band
    // TTN defines an additional channel at 869.525Mhz using SF9 for class B
    // devices' ping slots. LMIC does not have an easy way to define set this
    // frequency and support for class B is spotty and untested, so this
    // frequency is not configured here.

    // Disable link check validation
    LMIC_setLinkCheckMode(0);

    // TTN uses SF9 for its RX2 window.
    LMIC.dn2Dr = DR_SF9;

    // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
    LMIC_setDrTxpow(spreadFactor, 14);

    // Start job
    LMIC_startJoining();

    do_send(&sendjob); // Will fire up also the join

    Serial.println("LORA started");
}

void loopLMIC(void)
{
    os_runloop_once();
}