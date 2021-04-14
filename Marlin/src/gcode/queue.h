/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#pragma once

/**
 * queue.h - The G-code command queue, which holds commands before they
 *           go to the parser and dispatcher.
 */

#include "../inc/MarlinConfig.h"

class GCodeQueue {
public:
  /**
   * The buffers per serial port.
   */
  struct SerialState {
    /**
     * GCode line number handling. Hosts may include line numbers when sending
     * commands to Marlin, and lines will be checked for sequentiality.
     * M110 N<int> sets the current line number.
     */
    long last_N;
    int count;                        //!< Number of characters read in the current line of serial input
    char line_buffer[MAX_CMD_SIZE];   //!< The current line accumulator
    uint8_t input_state;              //!< The input state
  };

  static SerialState serial_state[NUM_SERIAL]; //!< Serial states for each serial port

  /**
   * GCode Command Queue
   * A simple (circular) ring buffer of BUFSIZE command strings.
   *
   * Commands are copied into this buffer by the command injectors
   * (immediate, serial, sd card) and they are processed sequentially by
   * the main loop. The gcode.process_next_command method parses the next
   * command and hands off execution to individual handler functions.
   */
  struct CommandLine {
    char buffer[MAX_CMD_SIZE];                    //!< The command buffer
    bool skip_ok;                                 //!< Skip sending ok when command is processed?
    TERN_(HAS_MULTI_SERIAL, serial_index_t port); //!< Serial port the command was received on
  };

  /**
   * A handy ring buffer type
   */
  struct RingBuffer {
    uint8_t length,                 //!< Number of commands in the queue
            index_r,                //!< Ring buffer's read position
            index_w;                //!< Ring buffer's write position
    CommandLine commands[BUFSIZE];  //!< The ring buffer of commands

    inline serial_index_t command_port() const { return TERN0(HAS_MULTI_SERIAL, commands[index_r].port); }

    inline void clear() { length = index_r = index_w = 0; }

    void advance_pos(uint8_t &p, const int inc) { if (++p >= BUFSIZE) p = 0; length += inc; }

    void commit_command(bool skip_ok
      #if HAS_MULTI_SERIAL
        , serial_index_t serial_ind=-1
      #endif
    );

    bool enqueue(const char* cmd, bool skip_ok = true
      #if HAS_MULTI_SERIAL
        , serial_index_t serial_ind=-1
      #endif
    );

    void ok_to_send();

    inline bool full(uint8_t cmdCount=1) const { return length > (BUFSIZE - cmdCount); }

    inline bool occupied() const { return length != 0; }

    inline bool empty() const { return !occupied(); }

    inline CommandLine& peek_next_command() { return commands[index_r]; }

    inline char* peek_next_command_string() { return peek_next_command().buffer; }
  };

  /**
   * The ring buffer of commands
   */
  static RingBuffer ring_buffer;

  /**
   * Clear the Marlin command queue
   */
  static void clear() { ring_buffer.clear(); }

  /**
   * Next Injected Command (PROGMEM) pointer. (nullptr == empty)
   * Internal commands are enqueued ahead of serial / SD commands.
   */
  static PGM_P injected_commands_P;

  /**
   * Injected Commands (SRAM)
   */
  static char injected_commands[64];

  /**
   * Enqueue command(s) to run from PROGMEM. Drained by process_injected_command_P().
   * Don't inject comments or use leading spaces!
   * Aborts the current PROGMEM queue so only use for one or two commands.
   */
  static inline void inject_P(PGM_P const pgcode) { injected_commands_P = pgcode; }

  /**
   * Enqueue command(s) to run from SRAM. Drained by process_injected_command().
   * Aborts the current SRAM queue so only use for one or two commands.
   */
  static inline void inject(char * const gcode) {
    strncpy(injected_commands, gcode, sizeof(injected_commands) - 1);
  }

  /**
   * Enqueue and return only when commands are actually enqueued
   */
  static void enqueue_one_now(const char* cmd);

  /**
   * Attempt to enqueue a single G-code command
   * and return 'true' if successful.
   */
  static bool enqueue_one_P(PGM_P const pgcode);

  /**
   * Enqueue from program memory and return only when commands are actually enqueued
   */
  static void enqueue_now_P(PGM_P const cmd);

  /**
   * Check whether there are any commands yet to be executed
   */
  static bool has_commands_queued() { return ring_buffer.length || injected_commands_P || injected_commands[0]; }

  /**
   * Get the next command in the queue, optionally log it to SD, then dispatch it
   */
  static void advance();

  /**
   * Run the entire queue in-place
   */
  static void exhaust();

  /**
   * Add to the circular command queue the next command from:
   *  - The command-injection queue (injected_commands_P)
   *  - The active serial input (usually USB)
   *  - The SD card file being actively printed
   */
  static void get_available_commands();

  /**
   * Send an "ok" message to the host, indicating
   * that a command was successfully processed.
   *
   * If ADVANCED_OK is enabled also include:
   *   N<int>  Line number of the command, if any
   *   P<int>  Planner space remaining
   *   B<int>  Block queue space remaining
   */
  static inline void ok_to_send() { ring_buffer.ok_to_send(); }

  /**
   * Clear the serial line and request a resend of
   * the next expected line number.
   */
  static void flush_and_request_resend(const serial_index_t serial_ind);

  /**
   * (Re)Set the current line number for the last received command
   */
  static inline void set_current_line_number(long n) { serial_state[ring_buffer.command_port()].last_N = n; }

private:

  static void get_serial_commands();

  #if ENABLED(SDSUPPORT)
    static void get_sdcard_commands();
  #endif

  // Process the next "immediate" command (PROGMEM)
  static bool process_injected_command_P();

  // Process the next "immediate" command (SRAM)
  static bool process_injected_command();

  /**
   * Enqueue with Serial Echo
   * Return true on success
   */
  static bool enqueue_one(const char* cmd);

  static void gcode_line_error(PGM_P const err, const serial_index_t serial_ind);

  friend class GcodeSuite;
};

extern GCodeQueue queue;

extern const char G28_STR[];
