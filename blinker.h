class Blinker {
  private:
    uint8_t pin;           // Broche de la DEL
    unsigned long interval;// Intervalle de clignotement (en ms)
    unsigned long previousMillis; // Dernier temps enregistré
    bool state;            // État actuel de la DEL (HIGH/LOW)
    bool active;           // Minuterie active ou en pause
    const unsigned long minInterval = 50; //Minimal interval

  public:
    // Constructeur : prend la broche et l'intervalle (ex: 500ms)
    Blinker(uint8_t ledPin, unsigned long blinkInterval) {
      pin = ledPin;
      interval = blinkInterval;
      previousMillis = 0;
      state = LOW;
      active = true;
    }

    // À appeler dans le setup()
    void begin() {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, state);
    }

    // À appeler en continu dans le loop()
    void update() {
      if (!active) return;

      unsigned long currentMillis = millis();
      if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        state = !state; // Inverse l'état (HIGH <-> LOW)
        digitalWrite(pin, state);
      }
    }

    // Méthodes optionnelles de contrôle
    void setInterval(unsigned long newInterval) {
      if (newInterval > minInterval ) interval = newInterval;
    }
    void start() { active = true; }
    bool start(unsigned long newInterval) {
      if (newInterval > minInterval ) {
        interval = newInterval;
        active = true;
        return true;
      } else return false;
    }
    void stop() { 
      active = false; 
      state = LOW; 
      digitalWrite(pin, state); 
    }
    void setLow() {
      this->stop();
    }
    void setHigh() {
      active = false; 
      state = HIGH; 
      digitalWrite(pin, state); 
    }
    bool getStatus() {
      return active;
    }
};