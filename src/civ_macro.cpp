#include "civ_macro.h"
#include "civ_hid.h"
#include <SD.h>

namespace {
enum class Phase {
  IDLE, TEST_DOWN, TEST_UP,
  PRE_DOWN, PRE_UP, ESC_DOWN, ESC_UP, PAD_DOWN, PAD_UP,
  CHAR_DOWN, CHAR_UP, ENTER1_DOWN, ENTER1_UP, ENTER2_DOWN, ENTER2_UP,
  BETWEEN, BETWEEN_DOWN, BETWEEN_UP
};
Phase phase=Phase::IDLE;
uint32_t due=0, fileLines=0, doneLines=0;
unsigned int charIndex=0;
int preamble=0;
bool wasCompleted=false;
String line, message="Not started";
File script;
const char* macroPath="/civswitch/bootstrap.txt";

void finish(const String &text, bool ok) {
  CivHid::releaseAll();
  if (script) script.close();
  phase=Phase::IDLE;
  message=text; wasCompleted=ok;
  Serial.println("HID macro: "+message);
}

bool nextLine() {
  while (script && script.available()) {
    line=script.readStringUntil('\n');
    if (line.endsWith("\r")) line.remove(line.length()-1);
    if (line.length()==0) continue;
    if (line.length()>253) { finish("Line longer than 253 chars",false); return false; }
    for (char c : line) {
      if (c<32 || c>126) { finish("Non-ASCII macro character",false); return false; }
    }
    charIndex=0;
    message="Line "+String(doneLines+1)+"/"+String(fileLines);
    return true;
  }
  finish("Macro sent (not exploit proof)", true);
  return false;
}
bool inputReady() {
  if (CivHid::ready()) return true;
  finish("HID host disconnected",false);
  return false;
}
} // namespace

bool CivMacro::inspect(const char* path) {
  if (phase!=Phase::IDLE) return false;
  File f=SD.open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    message="Missing /civswitch/bootstrap.txt";
    return false;
  }
  fileLines=0;
  while (f.available()) {
    String row=f.readStringUntil('\n');
    if (row.endsWith("\r")) row.remove(row.length()-1);
    if (!row.length()) continue;
    if (row.length()>253) { message="Bootstrap line >253 chars"; f.close(); return false; }
    for (char c : row) {
      if (c<32 || c>126) { message="Bootstrap has invalid chars"; f.close(); return false; }
    }
    fileLines++;
    if (fileLines>1024) { message="Too many bootstrap lines"; f.close(); return false; }
  }
  f.close();
  if (!fileLines) { message="Bootstrap file empty"; return false; }
  macroPath=path;
  message=String(fileLines)+" bootstrap lines ready";
  return true;
}

bool CivMacro::start() {
  if (phase!=Phase::IDLE || !CivHid::ready()) {
    message="HID host not connected";
    return false;
  }
  if (!inspect(macroPath)) return false;
  script=SD.open(macroPath, FILE_READ);
  if (!script) { message="Failed to reopen SD"; return false; }
  wasCompleted=false;
  doneLines=0;
  preamble=0;
  line="";
  phase=Phase::PRE_DOWN;
  due=millis()+2000; // time for user to release confirm key / check focus
  message="Starting HID macro";
  return true;
}

bool CivMacro::testA() {
  if (phase!=Phase::IDLE || !CivHid::ready()) {
    message="HID host not connected";
    return false;
  }
  wasCompleted=false;
  phase=Phase::TEST_DOWN;
  due=millis();
  message="Controller A test";
  return true;
}
void CivMacro::abort(const String &why) { finish(why,false); }
bool CivMacro::running() { return phase!=Phase::IDLE; }
bool CivMacro::completed() { return wasCompleted; }
uint32_t CivMacro::lineNumber() { return doneLines; }
uint32_t CivMacro::totalLines() { return fileLines; }
String CivMacro::status() { return message; }
int CivMacro::percent() {
  if (wasCompleted) return 100;
  if (!fileLines) return 0;
  return (int)((100UL*doneLines)/fileLines);
}

void CivMacro::tick() {
  if (phase==Phase::IDLE || (int32_t)(millis()-due)<0) return;
  uint32_t now=millis();
  if (!inputReady()) return;
  switch (phase) {
    case Phase::TEST_DOWN:
      if (!CivHid::gamepadA(true)) { finish("A HID report failed",false); return; }
      phase=Phase::TEST_UP; due=now+65; return;
    case Phase::TEST_UP:
      CivHid::gamepadA(false);
      finish("A sent; check Switch response",true);
      return;
    case Phase::PRE_DOWN:
      if (!CivHid::gamepadA(true)) { finish("Controller report failed",false); return; }
      phase=Phase::PRE_UP; due=now+50; return;
    case Phase::PRE_UP:
      CivHid::gamepadA(false);
      preamble++;
      if (preamble<5) { phase=Phase::PRE_DOWN; due=now+800; }
      else if (nextLine()) { phase=Phase::ESC_DOWN; due=now+800; }
      return;
    case Phase::ESC_DOWN:
      if (!CivHid::keyDownSpecial(CivHidKeys::ESC)) { finish("ESC report failed",false); return; }
      phase=Phase::ESC_UP; due=now+40; return;
    case Phase::ESC_UP:
      CivHid::keyUp(); phase=Phase::PAD_DOWN; due=now+800; return;
    case Phase::PAD_DOWN:
      if (!CivHid::gamepadA(true)) { finish("Gamepad A failed",false); return; }
      phase=Phase::PAD_UP; due=now+50; return;
    case Phase::PAD_UP:
      CivHid::gamepadA(false); phase=Phase::CHAR_DOWN; due=now+1200; return;
    case Phase::CHAR_DOWN:
      if (charIndex>=line.length()) { phase=Phase::ENTER1_DOWN; due=now; return; }
      if (!CivHid::keyDownAscii(line[charIndex])) {
        finish("Keyboard key report failed",false); return;
      }
      phase=Phase::CHAR_UP; due=now+40; return;
    case Phase::CHAR_UP:
      CivHid::keyUp(); charIndex++; phase=Phase::CHAR_DOWN; due=now+40; return;
    case Phase::ENTER1_DOWN:
      if (!CivHid::keyDownSpecial(CivHidKeys::ENTER)) { finish("Enter report failed",false); return; }
      phase=Phase::ENTER1_UP; due=now+40; return;
    case Phase::ENTER1_UP:
      CivHid::keyUp(); phase=Phase::ENTER2_DOWN; due=now+200; return;
    case Phase::ENTER2_DOWN:
      if (!CivHid::keyDownSpecial(CivHidKeys::ENTER)) { finish("Second Enter failed",false); return; }
      phase=Phase::ENTER2_UP; due=now+40; return;
    case Phase::ENTER2_UP:
      CivHid::keyUp(); doneLines++;
      if (nextLine()) { phase=Phase::BETWEEN; due=now+800; }
      return;
    case Phase::BETWEEN:
      phase=Phase::BETWEEN_DOWN; due=now; return;
    case Phase::BETWEEN_DOWN:
      if (!CivHid::gamepadA(true)) { finish("Between-lines A failed",false); return; }
      phase=Phase::BETWEEN_UP; due=now+50; return;
    case Phase::BETWEEN_UP:
      CivHid::gamepadA(false); phase=Phase::ESC_DOWN; due=now+1200; return;
    default: return;
  }
}
