#if defined(ARDUINO_ARCH_ESP8266)

#include <ESP8266WiFi.h>
#include "lwip/etharp.h"

#elif defined(ARDUINO_ARCH_ESP32)

#include <WiFi.h>
#include <esp_wifi.h>

#endif

#include <NetworkConnection.h>


namespace NetworkUtils {

  class NetworkMgr {

  public:

    typedef std::function<void()> YieldCallback;
    typedef std::function<void(unsigned int)> DelayCallback;


    NetworkMgr() {
      NetworkConnection::setup(this->useDhcp);
      this->resetWifi();
    }


    NetworkMgr* setYieldCallback(YieldCallback callback = nullptr) {
      this->yieldCallback = callback;
      return this;
    }


    NetworkMgr* setDelayCallback(DelayCallback callback = nullptr) {
      this->delayCallback = callback;
      return this;
    }


    NetworkMgr* setHostname(const char* value) {
      this->hostname = value;
      return this;
    }


    NetworkMgr* setApCredentials(
      const char* ssid,
      const char* password = nullptr,
      uint8_t channel = 0
    ) {
      this->apName = ssid;
      this->apPassword = password;
      this->apChannel = channel;

      return this;
    }


    NetworkMgr* setStaCredentials(
      const char* ssid = nullptr,
      const char* password = nullptr,
      uint8_t channel = 0
    ) {
      this->staSsid = ssid;
      this->staPassword = password;
      this->staChannel = channel;

      return this;
    }


    NetworkMgr* setUseDhcp(bool value) {
      this->useDhcp = value;

      NetworkConnection::setup(this->useDhcp);

      return this;
    }


    NetworkMgr* setStaticConfig(
      const char* ip,
      const char* gateway,
      const char* subnet,
      const char* dns
    ) {
      this->staticIp.fromString(ip);
      this->staticGateway.fromString(gateway);
      this->staticSubnet.fromString(subnet);
      this->staticDns.fromString(dns);

      return this;
    }


    NetworkMgr* setStaticConfig(
      IPAddress& ip,
      IPAddress& gateway,
      IPAddress& subnet,
      IPAddress& dns
    ) {
      this->staticIp = ip;
      this->staticGateway = gateway;
      this->staticSubnet = subnet;
      this->staticDns = dns;

      return this;
    }


    bool hasStaCredentials() {
      return this->staSsid != nullptr && strlen(this->staSsid) > 0;
    }


    bool isConnected() {
      return this->isStaEnabled()
        && NetworkConnection::getStatus()
        == NetworkConnection::Status::CONNECTED;
    }


    bool isConnecting() {
      return this->isStaEnabled()
        && NetworkConnection::getStatus()
        == NetworkConnection::Status::CONNECTING;
    }


    bool isStaEnabled() {
      return (WiFi.getMode() & WIFI_STA) != 0;
    }


    bool isApEnabled() {
      return (WiFi.getMode() & WIFI_AP) != 0;
    }


    bool hasApClients() {
      if (!this->isApEnabled()) {
        return false;
      }

      return WiFi.softAPgetStationNum() > 0;
    }


    short int getRssi() {
      return WiFi.RSSI();
    }


    IPAddress getApIp() {
      return WiFi.softAPIP();
    }


    IPAddress getStaIp() {
      return WiFi.localIP();
    }


    IPAddress getStaSubnet() {
      return WiFi.subnetMask();
    }


    IPAddress getStaGateway() {
      return WiFi.gatewayIP();
    }


    IPAddress getStaDns() {
      return WiFi.dnsIP();
    }


    String getStaMac() {
      return WiFi.macAddress();
    }


    const char* getStaSsid() {
      return this->staSsid;
    }


    const char* getStaPassword() {
      return this->staPassword;
    }


    uint8_t getStaChannel() {
      return this->staChannel;
    }


    /*
     * Completely reset Wi-Fi.
     *
     * IMPORTANT:
     * This function is intentionally NOT used for ordinary reconnects.
     *
     * A complete Wi-Fi reset is expensive and can cause unnecessary
     * interruptions on the LAN.
     */
    bool resetWifi() {

      /*
       * Do NOT force a fake country such as JP.
       *
       * The previous implementation forced:
       *
       *     JP / channels 1-14 / MANUAL
       *
       * This can conflict with the actual AP/regulatory configuration.
       *
       * We allow the ESP32 Wi-Fi driver/AP information to determine
       * the operating country instead.
       */


      WiFi.persistent(false);


      #if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3

      WiFi.setAutoConnect(false);

      #endif


      /*
       * Automatic reconnect is disabled because OTGateway has its own
       * reconnect state machine.
       */
      WiFi.setAutoReconnect(false);


      #ifdef ARDUINO_ARCH_ESP8266

      /*
       * Disable Wi-Fi modem sleep on ESP8266.
       */
      WiFi.setSleepMode(WIFI_NONE_SLEEP);

      #elif defined(ARDUINO_ARCH_ESP32)

      /*
       * IMPORTANT:
       *
       * Disable Wi-Fi power saving.
       *
       * This is especially important for a device where low latency
       * LAN communication is more important than a small reduction
       * in power consumption.
       */
      WiFi.setSleep(WIFI_PS_NONE);

      #endif


      /*
       * Stop AP if it is running.
       */
      WiFi.softAPdisconnect();


      /*
       * Disconnect STA.
       */
      WiFi.disconnect(false, true);


      #ifdef ARDUINO_ARCH_ESP8266

      /*
       * Limit DHCP retries.
       */
      wifi_station_dhcpc_set_maxtry(5);

      #endif


      #if defined(ARDUINO_ARCH_ESP32) && ESP_ARDUINO_VERSION_MAJOR < 3

      /*
       * Arduino-ESP32 < 3 has known problems with completely
       * turning Wi-Fi off.
       */
      return true;

      #else

      return WiFi.mode(WIFI_OFF);

      #endif
    }


    /*
     * Request reconnect.
     *
     * We don't immediately reset Wi-Fi.
     */
    void reconnect() {
      this->reconnectFlag = true;
    }


    /*
     * Connect to configured STA.
     *
     * IMPORTANT:
     *
     * force=true no longer means "completely reset Wi-Fi".
     * Ordinary reconnects use disconnect + begin instead.
     */
    bool connect(
      bool force = false,
      unsigned int timeout = 1000u
    ) {

      if (this->isConnected()) {
        return true;
      }


      /*
       * Stop an existing connection attempt before starting
       * a new one.
       *
       * Do NOT perform resetWifi() here.
       */
      this->disconnect();


      if (!this->hasStaCredentials()) {
        return false;
      }


      this->delayCallback(100);


      #ifdef ARDUINO_ARCH_ESP32

      /*
       * Set hostname before starting STA.
       */
      if (this->setWifiHostname(this->hostname)) {

        Log.straceln(
          FPSTR(L_NETWORK),
          F("Set hostname '%s': success"),
          this->hostname
        );

      } else {

        Log.serrorln(
          FPSTR(L_NETWORK),
          F("Set hostname '%s': fail"),
          this->hostname
        );
      }

      #endif


      /*
       * Enable STA without unnecessarily changing AP state.
       */
      if (!WiFi.mode(
        (WiFiMode_t)(WiFi.getMode() | WIFI_STA)
      )) {
        return false;
      }


      this->delayCallback(100);


      #ifdef ARDUINO_ARCH_ESP8266

      if (this->setWifiHostname(this->hostname)) {

        Log.straceln(
          FPSTR(L_NETWORK),
          F("Set hostname '%s': success"),
          this->hostname
        );

      } else {

        Log.serrorln(
          FPSTR(L_NETWORK),
          F("Set hostname '%s': fail"),
          this->hostname
        );
      }

      this->delayCallback(100);

      #endif


      #ifdef ARDUINO_ARCH_ESP32

      /*
       * FAST_SCAN:
       *
       * Do not scan every Wi-Fi channel on every reconnect.
       *
       * If staChannel is supplied, WiFi.begin() also receives it
       * as a channel hint.
       */
      WiFi.setScanMethod(WIFI_FAST_SCAN);

      /*
       * If several APs use the same SSID, prefer the strongest one.
       */
      WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

      #endif


      /*
       * Static IP configuration.
       *
       * WiFi.config() should be applied BEFORE WiFi.begin().
       */
      if (!this->useDhcp) {

        if (!WiFi.config(
          this->staticIp,
          this->staticGateway,
          this->staticSubnet,
          this->staticDns
        )) {

          Log.swarningln(
            FPSTR(L_NETWORK),
            F("WiFi static config failed")
          );
        }
      }


      /*
       * Start connection.
       */
      WiFi.begin(
        this->staSsid,
        this->staPassword,
        this->staChannel,
        nullptr,
        true
      );


      /*
       * Wait for connection.
       */
      unsigned long beginConnectionTime = millis();


      while (
        millis() - beginConnectionTime < timeout
      ) {

        this->delayCallback(100);


        NetworkConnection::Status status =
          NetworkConnection::getStatus();


        if (
          status != NetworkConnection::Status::CONNECTING
          &&
          status != NetworkConnection::Status::NONE
        ) {

          return
            status == NetworkConnection::Status::CONNECTED;
        }
      }


      return false;
    }


    /*
     * Disconnect without completely resetting Wi-Fi.
     */
    void disconnect() {

      #ifdef ARDUINO_ARCH_ESP32

      WiFi.disconnectAsync(false, true);


      const unsigned long start = millis();


      while (
        WiFi.isConnected()
        &&
        (millis() - start) < 5000
      ) {

        this->delayCallback(100);
      }


      #else

      WiFi.disconnect(false, true);

      #endif
    }


    void loop() {

      /*
       * User/application requested reconnect.
       */
      if (this->reconnectFlag) {

        this->delayCallback(500);


        Log.sinfoln(
          FPSTR(L_NETWORK),
          F("Reconnecting...")
        );


        this->reconnectFlag = false;


        /*
         * IMPORTANT:
         *
         * No resetWifi() here.
         */
        this->disconnect();


        NetworkConnection::reset();


        this->delayCallback(500);


        return;
      }


      /*
       * Connected but STA credentials disappeared.
       */
      if (
        this->isConnected()
        &&
        !this->hasStaCredentials()
      ) {

        Log.sinfoln(
          FPSTR(L_NETWORK),
          F("Reset")
        );


        this->resetWifi();


        NetworkConnection::reset();


        this->delayCallback(1000);


        return;
      }


      /*
       * Connected.
       */
      if (this->isConnected()) {

        if (!this->connected) {

          this->connectedTime = millis();
          this->connected = true;


          Log.sinfoln(
            FPSTR(L_NETWORK),
            F(
              "Connected, downtime: %lu s., "
              "IP: %s, RSSI: %hhd"
            ),
            (millis() - this->disconnectedTime) / 1000,
            WiFi.localIP().toString().c_str(),
            WiFi.RSSI()
          );
        }


        /*
         * Stop AP after STA has been stable.
         */
        if (
          this->isApEnabled()
          &&
          millis() - this->connectedTime
            > this->reconnectInterval
          &&
          !this->hasApClients()
        ) {

          Log.sinfoln(
            FPSTR(L_NETWORK),
            F("Stop AP because STA connected")
          );


          WiFi.mode(WIFI_STA);


          return;
        }


        #ifdef ARDUINO_ARCH_ESP8266

        /*
         * Gratuitous ARP keep-alive.
         */
        if (
          millis() - this->prevArpGratuitous
          > 60000
        ) {

          this->stationKeepAliveNow();

          this->prevArpGratuitous = millis();
        }

        #endif


        return;
      }


      /*
       * Not connected.
       */
      if (this->connected) {

        this->disconnectedTime = millis();
        this->connected = false;


        Log.sinfoln(
          FPSTR(L_NETWORK),
          F(
            "Disconnected, reason: %d, uptime: %lu s."
          ),
          NetworkConnection::getDisconnectReason(),
          (millis() - this->connectedTime) / 1000
        );
      }


      /*
       * No credentials -> start AP.
       */
      if (
        !this->hasStaCredentials()
        &&
        !this->isApEnabled()
      ) {

        Log.sinfoln(
          FPSTR(L_NETWORK),
          F("No STA credentials, start AP")
        );


        WiFi.mode(WIFI_AP_STA);

        this->delayCallback(250);

        WiFi.softAP(
          this->apName,
          this->apPassword,
          this->apChannel
        );


        return;
      }


      /*
       * STA disconnected for a long time -> AP.
       */
      if (
        !this->isApEnabled()
        &&
        millis() - this->disconnectedTime
          > this->failedConnectTimeout
      ) {

        Log.sinfoln(
          FPSTR(L_NETWORK),
          F(
            "Disconnected for a long time, start AP"
          )
        );


        WiFi.mode(WIFI_AP_STA);

        this->delayCallback(250);

        WiFi.softAP(
          this->apName,
          this->apPassword,
          this->apChannel
        );


        return;
      }


      /*
       * Connection attempt is stuck.
       *
       * Only here do we perform a full Wi-Fi reset.
       */
      if (
        this->isConnecting()
        &&
        millis() - this->prevReconnectingTime
          > this->resetConnectionTimeout
      ) {

        Log.swarningln(
          FPSTR(L_NETWORK),
          F(
            "Connection timeout, "
            "reset WiFi..."
          )
        );


        this->resetWifi();

        NetworkConnection::reset();

        this->delayCallback(500);


        return;
      }


      /*
       * Normal reconnect.
       *
       * IMPORTANT CHANGE:
       *
       * Previously:
       *
       *     connect(true, ...)
       *
       * which called resetWifi().
       *
       * Now:
       *
       *     connect(false, ...)
       *
       * which performs a normal disconnect/begin.
       */
      if (
        !this->isConnecting()
        &&
        this->hasStaCredentials()
        &&
        (
          !this->prevReconnectingTime
          ||
          millis() - this->prevReconnectingTime
            > this->reconnectInterval
        )
      ) {

        Log.sinfoln(
          FPSTR(L_NETWORK),
          F("Try connect...")
        );


        NetworkConnection::reset();


        if (
          !this->connect(
            false,
            this->connectionTimeout
          )
        ) {

          Log.straceln(
            FPSTR(L_NETWORK),
            F(
              "Connection failed. "
              "Status: %d, reason: %d, "
              "raw reason: %d"
            ),
            NetworkConnection::getStatus(),
            NetworkConnection::getDisconnectReason(),
            NetworkConnection::rawDisconnectReason
          );
        }


        this->prevReconnectingTime = millis();
      }
    }


    static uint8_t rssiToSignalQuality(
      short int rssi
    ) {

      return constrain(
        map(rssi, -100, -50, 0, 100),
        0,
        100
      );
    }


  protected:


    /*
     * Normal reconnect interval.
     *
     * We don't reset the Wi-Fi hardware every 15 seconds anymore.
     */
    const unsigned int reconnectInterval = 15000;


    /*
     * After 185 seconds without connection,
     * AP mode becomes available.
     */
    const unsigned int failedConnectTimeout = 185000;


    /*
     * Normal connection attempt timeout.
     */
    const unsigned int connectionTimeout = 10000;


    /*
     * Only a genuinely stuck connection attempt causes
     * a complete Wi-Fi reset.
     */
    const unsigned int resetConnectionTimeout = 90000;


    YieldCallback yieldCallback = []() {
      ::yield();
    };


    DelayCallback delayCallback = [](unsigned int time) {
      ::delay(time);
    };


    const char* hostname = "esp";

    const char* apName = "ESP";

    const char* apPassword = nullptr;

    uint8_t apChannel = 1;


    const char* staSsid = nullptr;

    const char* staPassword = nullptr;

    uint8_t staChannel = 0;


    bool useDhcp = true;


    IPAddress staticIp;

    IPAddress staticGateway;

    IPAddress staticSubnet;

    IPAddress staticDns;


    bool connected = false;

    bool reconnectFlag = false;


    unsigned long prevArpGratuitous = 0;

    unsigned long prevReconnectingTime = 0;

    unsigned long connectedTime = 0;

    unsigned long disconnectedTime = 0;


    bool setWifiHostname(const char* hostname) {

      if (!this->isHostnameValid(hostname)) {
        return false;
      }


      const char* currentHostname = WiFi.getHostname();


      if (
        currentHostname != nullptr
        &&
        strcmp(currentHostname, hostname) == 0
      ) {
        return true;
      }


      return WiFi.setHostname(hostname);
    }


    #ifdef ARDUINO_ARCH_ESP8266

    /**
     * @brief Send gratuitous ARP packet.
     */
    static void stationKeepAliveNow(void) {

      for (
        netif* interface = netif_list;
        interface != nullptr;
        interface = interface->next
      ) {

        if (
          (interface->flags & NETIF_FLAG_LINK_UP)
          &&
          (interface->flags & NETIF_FLAG_UP)
          &&
          interface->num == STATION_IF
          &&
          (!ip4_addr_isany_val(
            *netif_ip4_addr(interface)
          ))
        ) {

          etharp_gratuitous(interface);

          break;
        }
      }
    }

    #endif


    /**
     * @brief Check RFC hostname compliance.
     */
    static bool isHostnameValid(
      const char* value
    ) {

      if (value == nullptr) {
        return false;
      }


      size_t len = strlen(value);


      /*
       * Fix:
       *
       * Previously an empty hostname caused:
       *
       *     value[len - 1]
       *
       * which means value[-1].
       */
      if (len == 0 || len > 24) {
        return false;
      }


      if (value[len - 1] == '-') {
        return false;
      }


      for (size_t i = 0; i < len; i++) {

        if (
          !isalnum(
            static_cast<unsigned char>(value[i])
          )
          &&
          value[i] != '-'
        ) {

          return false;
        }
      }


      return true;
    }
  };
}