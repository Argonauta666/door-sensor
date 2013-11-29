/* 
 * This code receives the codes sent by wireless magnetic door sensors using the
 * SC2262 chip. These chips send the code in the following format, which was
 * determined experimentally:
 *
 *  19 times:
 *      12 times:
 *          - Send a tri-state "bit":
 *              - 0: Short high, long low, short high, long low
 *                          OR
 *              - 1: Long high, short low, long high, short low
 *                          OR
 *              - F: Short high, long low, long high, short low
 *          - Send a short high.
 *      - Delay about 10 to 15 milliseconds (low).
 *  - Send some other tri-state bits (not sure what they are).
 *
 * The tri-state "bits" look like this:
 *
 *   0:
 *     ___           ___
 *    | x |         | x |
 *  __|   |_________|   |_________
 *            3x            3x
 *
 *   1:
 *     _________     _________
 *    |   3x    |   |   3x    |
 *  __|         |___|         |___
 *                x             x
 *
 *   F:
 *
 *     ___           _________
 *    | x |         |   3x    |
 *  __|   |_________|         |___
 *            3x                x
 *
 * Images of the actual signal: http://imgur.com/a/ZK8tu
 */ 

// The pin the receiver is attached to.
int pin = 2;

// The minimum duration of a short pulse. A high pulse longer than this but
// shorter than long_min will be recognized as a SHORT pulse.
int short_min = 350;

// The minimum duration of a long pulse. A high pulse longer than this will be
// recognized as a LONG pulse.
int long_min = 1200;

// The code is sent 19 times, but we don't want to try to read them all, just to
// give us a little margin in case we get out of alignment.
int resend_count = 17;

void setup() {
    Serial.begin(9600);
    pinMode(pin, INPUT);
}

// Waits up to 50ms until 3ms of consecutive LOW is observed.
// This is used to find the next "gap" between repetitions of the code when
// we're out-of-alignment.
void findNextSpacer()
{
    int time = 0;
    int total_time = 0;
    int measure_delay = 10;
    while (time < 3000 && total_time < 50000) {
        if (digitalRead(pin) == LOW) {
            time += measure_delay; 
        } else {
            time = 0; 
        }
        total_time += measure_delay;
        delayMicroseconds(measure_delay);
    } 
}

// Call this in the long LOW before the code starts and it will be read into buf
// and 1 will be returned. If the next pulses do not form a valid code, or if
// a code was only partially read, 0 is returned.
int readNextCode(char buf[13])
{
    unsigned long first, second;
    int i;
    for (i = 0; i < 12; i++) {

        /* Read the first pulse duration. */
        first = pulseIn(pin, HIGH);

        /* If there was no pulse, or it was shorter than what we expect a short
         * pulse to be, fail. */
        if (first == 0 || first < short_min) {
            /* If this isn't the first tri-state, there's a good chance it WAS
             * actually part of a code, but it got screwed up. In that case, we
             * have to wait for the next space between codes. */
            if (i >= 1) {
                Serial.print("DEBUG: ");
                Serial.print(i);
                Serial.print(": ");
                Serial.println(first);
                Serial.println("FAIL (first pulse)");
                findNextSpacer();
            }
            return 0;
        }

        /* Read the second pulse duration. */
        second = pulseIn(pin, HIGH);

        if (second == 0 || second < short_min) {
            /* Same as before, if it isn't the first, it's probably a screwed up
             * code, so wait for the next gap. */
            if (i >= 1) {
                Serial.print("DEBUG: ");
                Serial.print(i);
                Serial.print(": ");
                Serial.print(second);
                Serial.println("FAIL (second pulse)");
                findNextSpacer();
            }
            return 0; 
        }

        /* If both pulses are as long as a short pulse, we make it here, and we
         * can turn the pulse widths into the actual value. */

        if (first > long_min && second > long_min) {
            buf[i] = '1';
        } else if (second > long_min) {
            buf[i] = 'F'; 
        } else if (first < long_min && second < long_min) {
            buf[i] = '0'; 
        } else {
            /* If we get here, it's because we've seen a long pulse followed by
             * a short pulse, which isn't a valid tri-state. When this happens,
             * we just stop trying to read this code and wait until the gap
             * before the next one. */
            buf[i] = 'X';
            buf[i+1] = '\0';
            findNextSpacer();
            return 0;
        }
    }

    /* There's one extra short pulse after the code that isn't part of any of
     * the tri-states. I'm not sure why it's there, but we have to jump over it.
     * This delay should put us in the middle of the gap before the next code. */
    delayMicroseconds(5000);

    buf[12] = '\0';
    return 1;
}

void loop() {
    /* Records all of the code readings. */
    char codes[resend_count][13];
    /* Records whether the i-th code reading was successful. */
    char valid[resend_count];
    /* Used to count the frequency of tri-states to find the majority. */
    unsigned char majority[12][3];
    /* Holds the final result, after the majority has been taken. */
    char result[13];

    /* Zero the majority accumulator. */
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j < 3; j++) {
            majority[i][j] = 0;
        } 
    }
    
    /* Read up to resend_count codes. */
    int res = 0;
    int num_success = 0;
    for (int i = 0; i < resend_count; i++) {
        res = readNextCode(codes[i]);
        num_success += res;
        valid[i] = res;
        if (num_success == 0) {
            break;
        } 
    }

    /* If at least 3 were read successfully, we find the majority and use that. */
    if (num_success > 3) {

        /* Print out all of the received codes. We have to do this AFTER we get
         * them all, because the printing will screw up the timing. */
        Serial.println("DEBUG: ####");
        for (int i = 0; i < resend_count; i++) {
            if (!valid[i]) {
                Serial.println("DEBUG: <<Invalid>>");
                continue; 
            }
            Serial.print("DEBUG: ");
            Serial.println(codes[i]); 

            /* Add the "columns" to the majority counts. */
            for (int j = 0; j < 12; j++) {
                if (codes[i][j] == '0') { majority[j][0]++; }
                else if (codes[i][j] == '1') { majority[j][1]++; }
                else if (codes[i][j] == 'F') { majority[j][2]++; }  
            }
        }

        /* Find the "correct" code from the majority. If most of the received
         * codes are correct, then the majority of the "columns" should be
         * correct. */
        for (int i = 0; i < 12; i++) {
            if (majority[i][0] > majority[i][1] && majority[i][0] > majority[i][2]) {
                result[i] = '0';
            } else if (majority[i][1] > majority[i][2]) {
                result[i] = '1'; 
            } else {
                result[i] = 'F'; 
            }
        }
        result[12] = '\0';
        Serial.println(result);
        Serial.println("DEBUG: ####");

        /* Wait until we're sure all the code repetitions are finished before we
         * try to read the next ones. */
        delay(1250);
    }
}
