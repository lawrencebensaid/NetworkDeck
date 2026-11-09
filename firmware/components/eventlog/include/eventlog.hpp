// The dock's event log: what happened (starts and restarts, sign-ins, updates, network and Stream Deck
// app connections), kept in flash so it survives restarts and power loss.
//
// 8192 records of 128 bytes in a 1 MB ring at 0xF00000, past the partitions (the 16 MB flash is half
// used). New installs have it in partitions.csv; docks updated over the air get it registered at run
// time, at the same place. Writes go through a queue to a task, so callers never wait on the flash.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace eventlog {

enum class Cat : uint8_t { System, Update, Network, Connection, Deck, Security, Matter, Count };
enum class Level : uint8_t { Info, Warn, Error };

// Finds the newest record and starts the writer. Call once, early (before the first add()).
void start();

// Formats and queues a record (at most 107 characters). Safe from any task; never blocks.
void add(Cat cat, Level level, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

// {"capacity","kept","boot","now","uptime","records":[{"s","b","t","u","c","l","m"}...]}, newest first:
// at most `limit` records with a sequence number below `before` (0: from the newest).
//   s sequence number, b boot number, t Unix time (0 before the clock was set), u seconds since that
//   boot's start, c category, l level, m message.
std::string json(uint32_t before, size_t limit);

const char* cat_name(Cat c);

}  // namespace eventlog
