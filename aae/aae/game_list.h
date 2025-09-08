/*============================================================================
  game_list.h
  ----------------------------------------------------------------------------
  Circular, doubly-linked, alphabetically-sorted list of games for AAE
  ============================================================================

  • Builds itself from the null-terminated AAEDriver[] table that the
    AAE emulator core already defines.

  • Each entry is stored in a contiguous std::vector<GameNode> so you can
    access items by index *in sorted order* via operator[] — just like the
    old C array.

  • The list is also linked into a circular ring (next/prev pointers) so
    traversal forward or backward is O(1) with seamless wrap-around.

  • Helper indexOfGameNum() lets you pass an original driver index
    (gamenum) and get back the position of that game in the sorted list,
    or -1 if it isn’t present.

  • C++11-only: uses std::vector, std::string, emplace_back, nullptr,
    but no lambdas or auto in the public API for maximum readability.

  ----------------------------------------------------------------------------
  Example
  ----------------------------------------------------------------------------
      extern const AAEDriver driver[];

      GameList gList(driver);                 // build & sort once

      int selGame = 42;                       // AAEDriver index
      int idx     = gList.indexOfGameNum(selGame);
      if (idx >= 0)
          printf("Selected: %s\n",
                 gList[idx].displayName.c_str());

      // Forward traversal from head
      const GameNode* n = gList.head();
      do {
          // ... //
          n = n->next;
      } while (n != gList.head());
  ----------------------------------------------------------------------------
  License
  ----------------------------------------------------------------------------
     This is free and unencumbered software released into the public domain.

     Anyone is free to copy, modify, publish, use, compile, sell, or
     distribute this software, either in source code form or as a compiled
     binary, for any purpose, commercial or non-commercial, and by any
     means.

     In jurisdictions that recognize copyright laws, the author or authors
     of this software dedicate any and all copyright interest in the
     software to the public domain. We make this dedication for the benefit
     of the public at large and to the detriment of our heirs and
     successors. We intend this dedication to be an overt act of
     relinquishment in perpetuity of all present and future rights to this
     software under copyright law.

     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
     EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
     MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
     IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
     OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
     ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
     OTHER DEALINGS IN THE SOFTWARE.
  ============================================================================*/

#pragma once
#ifndef GAME_LIST_H
#define GAME_LIST_H

#include <vector>
#include <string>
#include <cstddef>

          // Forward declaration of AAEDriver (real struct lives elsewhere)
          struct AAEDriver;

      // ---------------------------------------------------------------------------
      // GameNode – single entry in the game list
      // ---------------------------------------------------------------------------
      struct GameNode
      {
          int         gameNum;        // original index in driver[]
          std::string displayName;    // printable title (desc fallback name)
          std::string description;      // full text or alternate label
          int         extraOptions;   // reserved for caller
          GameNode* next;           // clockwise link  (alphabetically later)
          GameNode* prev;           // counter-clockwise link

          GameNode() : gameNum(0),
              extraOptions(0),
              next(nullptr),
              prev(nullptr) {
          }
      };

      // ---------------------------------------------------------------------------
      // GameList – owns storage, sorting, and circular links
      // ---------------------------------------------------------------------------
      class GameList
      {
      public:
          GameList();
          explicit GameList(const AAEDriver* list);          // build immediately
          explicit GameList(const std::vector<const AAEDriver*>& reg); // NEW

          void              build(const AAEDriver* drivers); // (re)build
          void              build(const std::vector<const AAEDriver*>& reg);
          std::size_t       size() const;

          GameNode* head();
          const GameNode* head() const;

          const GameNode* findByGameNum(int gamenum) const;

          // Array-style access by *sorted* position
          GameNode& operator[](std::size_t index);
          const GameNode& operator[](std::size_t index) const;

          // NEW: return sorted index for a given driver index, or -1
          int               indexOfGameNum(int gamenum) const;

      private:
          static bool       compareNodes(const GameNode& a, const GameNode& b);
          void              linkCircular();

          std::vector<GameNode> nodes_;
          GameNode* head_;
      };

#endif /* GAME_LIST_H */
