// Suzuran's FO terminal fakery

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <time.h>
#include <ctype.h>

// Globals
int tries = 4;       // How many tries remain
int difficulty = 5;  // Difficulty (letters per word) (max 10)
char buf[90];        // Line buffer
char memory[24*16];  // Memory image being "hacked"

// Curses items
WINDOW *main_window = NULL; // The rest of the screen
WINDOW *log_window = NULL;  // Where the log text and prompt go

// Selectable Words
int words = 0;       // How many words are on the screen
int max_words = 14;  // How many words should we try to have
int word_index[25];  // Which words appear on screen
int word_cchar[25];  // Common Character Count
int word_addr[25];   // Address on the screen
int word_state[25];  // Redraw state
int winning_index = -1; // Which word is the winner
int max_duds = 3;    // How many words can be duds

// Word bank
#define MAX_WORD_BANK 4096
int  word_bank_top = 0;            // How many did we find?
char word_bank[MAX_WORD_BANK][11]; // Max difficulty+1

// Capped random number
int capped_rand(int max){
  int rv = 0;
  // Get bits
  rv = rand();  
  // If we have too much or nothing
  while(rv > max || rv == 0){
    // eliminate bits
    while(rv > max){
      rv >>= 1;
    }
    // If we lost all our bits, get more
    if(rv == 0){ rv = rand(); }
  }
  // Should be OK!
  return(rv);
}

void cleanup(){
  endwin();          // Done with curses
}

void find_words(){
  int x = 0,y = 0;   // Scratch
  int dud_flag = 0;  // Dud avoidance flag
  int last_addr = 0; // Last address used
  int duds = 0;      // Low-quality hints
  int winning_pos = 0; // Position in words list of winner
  FILE *fd;          // Dictionary file
  // Obtain dictionary
  fd = fopen("/usr/share/dict/words","r");
  if(fd == NULL){
    // Bail
    cleanup();
    printf("Error: Unable to open dictionary\n");
    exit(-1);
  }
  while(!feof(fd) && word_bank_top < MAX_WORD_BANK){
    char dictword[90];
    char *rv = NULL;
    int dictlen = 0;
    rv = fgets(dictword,89,fd);
    if(rv == NULL && !feof(fd)){
      cleanup();
      printf("Error while reading dictionary");
      exit(-1);
    }
    // Get len
    dictlen = strlen(dictword);
    // If word ends in NL
    if(dictword[dictlen-1] == '\n'){
      // Clobber NL
      dictword[dictlen-1] = 0;
      dictlen--;
    }
    // If word ends in S (there's a lot of those, discard them)
    if(dictword[dictlen-1] == 's'){
      continue;
    }
    // Length match?
    if(dictlen != difficulty){ continue; } // No, next!
    // Check remaining characters
    x = 0;
    while(x < dictlen){
      if((dictword[x] < 'A' || dictword[x] > 'Z') &&
	 (dictword[x] < 'a' || dictword[x] > 'z')){
	// Illegal character!
	x = 0xFF;
	break;
      }
      // Otherwise acceptable character. Store the uppercase equivalent.
      dictword[x] = toupper(dictword[x]);
      x++;
    }
    if(x == 0xFF){ continue; } // Skip when indicated
    // We have a candidate! Save it and continue
    strncpy(word_bank[word_bank_top],dictword,10);
    word_bank[word_bank_top][difficulty+1] = 0; // Ensure null termination
    word_bank_top++;
  }
  if(word_bank_top < 512){
    cleanup();
    printf("Error: Unable to find enough words to fill word bank\n");
    exit(-1);
  }
  // Pick a winner
  winning_index = capped_rand(word_bank_top-1);
  // We have a winner, so pick our candidates
  words = 0;
  // Initialize dud avoidance flag to somewhere in the first half
  dud_flag = capped_rand((max_words/2)-max_duds);
  // Continue
  while(words < max_words && words < word_bank_top){
    // Find word
    int candidate = capped_rand(word_bank_top-1);    
    int last_mx = -1; // Repeat match avoidance index
    x = 0;
    y = 0; // Number of matching characters
    if(candidate == winning_index){ continue; } // Don't pick the winner twice
    while(x < words){
      if(candidate == word_index[x]){ x = 0xFF; break; } // Don't pick a word we already picked
      x++;
    }
    if(x == 0xFF){ continue; } // Skip on flag
    // Check for common characters
    x = 0;
    y = 0;
    while(x < difficulty){
      // Match?
      if(word_bank[candidate][x] == word_bank[winning_index][x]){
	if(x == last_mx){
	  // We already did this
	  x = 0xFF; break;
	}
	// Keep note of this
	last_mx = x;
	y++; 
      }
      x++; // Next char
    }
    if(x == 0xFF){ continue; } // Rejected
    if(y < 1){
      // Not enough chars in common!
      if(duds < max_duds){
	// We can admit a dud. Is there time?
	if(dud_flag > 0){
	  dud_flag--;
	  continue; // No.
	}else{
	  // We'll take this one.
	  dud_flag = capped_rand(max_words-words); // Any subsequent word might be a dud
	  duds++;
	}	  
      }else{
	// Too many duds
	continue;
      }
    }
    word_index[words] = candidate; // Keep this one
    word_cchar[words] = y;         // Also save common char count
    word_state[words] = 0;         // Initialize this
    words++;
  }
  // Now that we have our word count, we can place them on the screen.
  winning_pos = (capped_rand(words+2)-1); // Maybe get zero? Or last one?
  // Make room for winning word
  y = words+1;
  // Move stuff
  while(y >= winning_pos){
    if(y == winning_pos){
      // Copy winner
      word_index[y] = winning_index;
      word_cchar[y] = difficulty;
    }else{
      // Copy word
      word_index[y] = word_index[y-1];
      word_cchar[y] = word_cchar[y-1];
    }
    // Next!
    y--;
  }
  x = 1;
  // Initialize last-used-address
  last_addr = capped_rand(36);
  word_addr[0] = last_addr;
  sprintf(buf,"WDS: %s(%d,%d) ",word_bank[word_index[0]],word_cchar[0],word_addr[0]);  
  last_addr += difficulty;
  // Iterate words
  while(x < words){
    // 24 positions per row, 16 rows
    int remaining_space = (24*16);
    remaining_space -= last_addr; // take out what we've used so far
    remaining_space -= (words-x)*(difficulty+1); // Take out the unused words plus one space each for padding
    if(remaining_space > 12){ remaining_space = 36; } // Cap this
    if(remaining_space > 0){
      // We can spend characters
      word_addr[x] = last_addr+capped_rand(remaining_space);
    }else{
      // Out of space! Retry.
      // word_addr[x] = last_addr+difficulty+1;
      last_addr = capped_rand(36);
      word_addr[0] = last_addr;
      last_addr += difficulty;
      x = 1;
    }
    sprintf(buf,"%s %s(%d,%d) ",buf,word_bank[word_index[x]],word_cchar[x],word_addr[x]);  
    last_addr = word_addr[x];
    x++;
  }
  // Now that we know what goes where, we can make our "memory" image
  // This is used for redrawing the screen on user input
  x = 0;
  y = 0;
  while(x < (24*16)){
    int rch;
    // Are we in a word?
    if(x >= word_addr[y]){
      // Yes. Still copying word?
      if(x < word_addr[y]+difficulty){
	// Yes.
	memory[x] = word_bank[word_index[y]][x-word_addr[y]];
	x++; continue;
      }else{
	// No, go to next word.
	y++;
      }
    }
    // Blank
    rch = capped_rand(16);
    rch += ' '; // Get punkt
    if(rch >= '('){ rch += 2; }  // Skip over parens
    if(rch >= ','){ rch++; }     // And these too
    if(rch >= '.'){ rch++; }
    if(rch >= '0'){ rch += 11; }
    if(rch >= '<'){ rch++; }
    if(rch >= '>'){ rch += 2; }
    if(rch >= 'A'){ rch += 29; }
    // Should be good enough...
    memory[x] = rch;
    x++;
  }
  // Done
  /* Print words found for debugging
  wmove(main_window,2,0);
  wprintw(main_window,buf);
  wmove(main_window,4,0);
  sprintf(buf,"Word bank has %d words, winner is (%d) %s",word_bank_top,winning_index,word_bank[winning_index]);
  wprintw(main_window,buf);
  */
}

// Initialize the screen
void screen_init(){
  // Initialize main screen
  if(main_window == NULL){
    erase();           // Clear the screen
    main_window = newwin(0,0,0,0);
  }else{
    werase(main_window);
  }
  if(main_window == NULL){
    cleanup();
    printf("Unable to make main window\n");
    exit(-1);
  }
  keypad(main_window,TRUE); // We want keypad keys
  // Draw header
  wmove(main_window,0,0);
  wprintw(main_window,"Welcome to ROBCO Industries (TM) Termlink");
  wmove(main_window,1,0);
  wprintw(main_window,"Password Required");
  wrefresh(main_window);
}

// Draw the main "game" screen
void draw_main(){
  uint16_t base_addr = 0; // Base address as printed on the screen
  int addr = 0;      // Real addr
  int row = 5;       // Where are we?
  int col = 0;       // Column
  // Initialize address
  base_addr = (rand()&0x7FF0);

  // Iterate rows
  while(row < 21){
    int x = 0; // Scratch
    wmove(main_window,row,col);

    // Address
    sprintf(buf,"0x%.4X ",base_addr+addr);
    wprintw(main_window,buf);
    wrefresh(main_window);
    
    // Print memory contents
    while(x < 12){
      sprintf(buf,"%c",memory[addr]);
      wprintw(main_window,buf);
      wrefresh(main_window);      
      // Advance address
      addr++;
      // Loop
      x++;
    }
    
    // Next!
    row++;
    if(row >= 21 && col == 0){
      row = 5; // Reset
      col = 21; // Move
    }
  }  
}

void draw_log(){
  log_window = newwin(16,16,5,41);
  if(log_window == NULL){
    cleanup();
    printf("Unable to make log window\n");
    exit(-1);
  }
  keypad(log_window,TRUE); // We want keypad keys
  scrollok(log_window,TRUE);
  wrefresh(log_window);
}

void draw_tries(){
  int x = 0;
  wmove(main_window,3,0);
  if(tries == 1){
    wattron(main_window,A_BLINK);    
  }
  wprintw(main_window,"Attempts Remaining:");
  // more dots
  wattron(main_window,A_ALTCHARSET);  
  while(x < tries){
    // sprintf(buf,"%s %c",buf,219);
    wprintw(main_window," %c",0140);
    x++;
  }
  wattroff(main_window,A_ALTCHARSET);
  // Clobber any leftovers 
  x = 0;
  while(x < 10){
    wprintw(main_window," ");
    x++;
  }
  if(tries == 1){
    wattroff(main_window,A_BLINK);    
  }
  // Done
  wrefresh(main_window);  
}

void do_failure(){
  int x = 0;
  int ch = 0;
  scrollok(main_window,TRUE); // Enable main window scroll
  delwin(log_window);         // Delete log window
  wmove(main_window,21,0);    // Move past end of memory 
  while(x < 25){
    if(x == 12){
      wprintw(main_window,"                TERMINAL LOCKED");
    }
    if(x == 14){
      wprintw(main_window,"       PLEASE CONTACT AN ADMINISTRATOR");
    }
    wprintw(main_window,"\n");
    wrefresh(main_window);
    x++;
  }
  scrollok(main_window,FALSE); // Done with main window scroll
  // Loop
  x = 0;  
  while(x == 0){
    ch = wgetch(main_window);
    if(ch == 'h'){
      x = 1;
    }
  }  
  // Reinitialize, reset, and return!
  tries = 4;
  screen_init();
  find_words();
  draw_main();
  draw_log();
  draw_tries();  
}

int main(){
  int ch = 0;        // Scratch
  int done = 0;      // Done-ness
  int csr_addr = 0; // Cursor address
  int csr_row = 0,csr_col = 0; // "Game" Cursor position
  srand(time(NULL)); // Seed RNG
  
  initscr();         // Init curses
  raw();             // Disable line buffering
  noecho();          // Don't echo keys

  // Initialize screen
  screen_init();
  // Find our word list
  find_words();
  // Draw main screen
  draw_main();
  // Draw log and prompt
  draw_log();
  // Draw tries
  draw_tries();
  
  // Busy loop here
  while(done == 0){
    int scr_row,scr_col; // "Screen" Cursor position
    int x;               // Scratch
    int selected_word = -1;
    // Determine cursor address
    if(csr_col > 11){
      // Right half
      csr_addr = (csr_row+16)*12; 
      csr_addr += csr_col-12;
    }else{
      // Left half
      csr_addr = csr_row*12;
      csr_addr += csr_col;
    }
    // Draw prompt
    // mvwprintw(log_window,15,0,">");
    // Are we over a word?
    x = 0;    
    while(x < words){
      int rd_row,rd_col,rd_x; // For redrawing words
      if(csr_addr >= word_addr[x] && csr_addr <= word_addr[x]+difficulty){
	// After the start and before the end!
	selected_word = x;
	if(word_state[x] == 0){
	  // Needs highlight added
	  rd_x = 0;
	  wattron(main_window,A_REVERSE);
	  while(rd_x < difficulty){
	    rd_row = ((word_addr[x]+rd_x)/12);
	    rd_col = ((word_addr[x]+rd_x)-(rd_row*12));
	    if(rd_row >= 16){ rd_row -= 16; rd_col += 21; }
	    mvwprintw(main_window,rd_row+5,rd_col+7,"%c",word_bank[word_index[x]][rd_x]);
	    rd_x++;
	  }
	  wattroff(main_window,A_REVERSE);
	  word_state[x] = 1;
	}
      }else{
	// Not selected
	if(word_state[x] == 1){
	  // Needs highlight removed
	  rd_x = 0;
	  while(rd_x < difficulty){
	    rd_row = ((word_addr[x]+rd_x)/12);
	    rd_col = ((word_addr[x]+rd_x)-(rd_row*12));
	    if(rd_row >= 16){ rd_row -= 16; rd_col += 21; }
	    mvwprintw(main_window,rd_row+5,rd_col+7,"%c",word_bank[word_index[x]][rd_x]);
	    rd_x++;
	  }
	  word_state[x] = 0;
	}
      }
      x++;
    }
    if(selected_word == -1){
      mvwprintw(log_window,15,0,">%c          ",memory[csr_addr]);
      wrefresh(log_window);
    }else{
      mvwprintw(log_window,15,0,">%s",word_bank[word_index[selected_word]]);
      wrefresh(log_window);
    }
    // Make screen cursor position
    scr_row = csr_row;
    if(csr_col > 11){
      scr_col = csr_col+9;
    }else{
      scr_col = csr_col;
    }
    // Go there
    wmove(main_window,scr_row+5,scr_col+7);
    // wprintw(main_window,"_");
    wrefresh(main_window);
    // Obtain input
    ch = wgetch(main_window);      // Get something
    if(ch == 'q'){ done = 1; }     // Die on Q
    // Selection?
    if(ch == KEY_ENTER || ch == '\n' || ch == '\r'){
      // Handle it
      if(selected_word == -1){
	mvwprintw(log_window,15,0,">%c          ",memory[csr_addr]);	
	wrefresh(log_window);
	wscrl(log_window,1);
	mvwprintw(log_window,15,0,">Error");
	wrefresh(log_window);
	wscrl(log_window,1);
	wrefresh(log_window);	
      }else{
	mvwprintw(log_window,15,0,">%s",word_bank[word_index[selected_word]]);
	wrefresh(log_window);
	wscrl(log_window,1);
	wrefresh(log_window);	
	if(word_cchar[selected_word] == difficulty){
	  // WINNER WINNER CHICKEN DINNER
	  cleanup();
	  erase();
	  printf("> Password Accepted.\n");
	  exit(0);
	}else{
	  // YOU FAIL IT
	  mvwprintw(log_window,15,0,">Entry denied.");
	  wrefresh(log_window);
	  wscrl(log_window,1);
	  mvwprintw(log_window,15,0,">Likeness=%d",word_cchar[selected_word]);
	  wscrl(log_window,1);
	  wrefresh(log_window);
	  tries--;
	  if(tries > 0){
	    draw_tries();
	  }else{
	    do_failure();
	    csr_addr = 0;
	    csr_row = 0;
	    csr_col = 0;
	    ch = 0; // Go around
	  }
	}
      }
    }
    // Cursor movement is screenwise
    if(ch == KEY_RIGHT && csr_col < 23){ csr_col++; }
    if(ch == KEY_LEFT && csr_col > 0){ csr_col--; }
    if(ch == KEY_UP && csr_row > 0){ csr_row--; }
    if(ch == KEY_DOWN && csr_row < 15){ csr_row++; }
  }
  cleanup();
  return(0);
}

