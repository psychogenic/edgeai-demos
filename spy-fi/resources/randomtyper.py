#!/usr/bin/env python3
'''
    Quick script to prompt me to type a variety of sentences
    that include only my target letters

'''
import random
import time
import sys

IncludeForcedLetters = True




# sentences (only using A, C, D, E, H, I, L, N, O, R, S, T and spaces, periods plus BACKSPACE)
sentences = [
    "The old cat chased the red rat.",
    "A thin child read the old tales.",
    "The sharp cat stole the cheese.",
    "Children danced in the rain.",
    "The doctor healed the sick cat.",
    "She read the ancient scroll.",
    "A cold rain hit the old shed.",
    "The horse ran in the desert.",
    "Rich lads stole the red cloth.",
    "The stern teacher scolded the class.",

    "The ancient scholar read the old scroll.",
    "A red cardinal sat on the thin branch.",
    "The clever artist painted the old castle.",
    "Children cheered the saint in the hall.",
    "The doctor sent the sick child to rest.",
    "She told the sad tale to the class.",
    "A stern leader controlled the entire land.",
    "The horse trotted in the desert sand.",
    "Rich traders sold the rare cloth.",
    "The lion chased the deer in the rain.",

    "The ancient historian read the rare doctrine.",
    "A radical leader controlled the northern district.",
    "The clever director handled the entire cast.",
    "Children listened to the old sailor tales.",
    "The saint healed the ill heart condition.",
    "Rich traders sold the rare oriental cloth.",
    "The hostile critic read the short article.",
    "A stern chancellor entered the old cathedral.",
    "The lion chased the deer across the desert.",
    "She told the classic tale to the scholars.",
    "The artist painted the ancient castle hall.",
    "A cold northern wind hit the old district.",
    "The doctor treated the rare skin condition.",
    "Rich lords held the secret election.",
    "The sailor charted the distant island.",
    "Children cheered the heroic saint.",
    "The scholar noticed the hidden code.",
    "A harsh dictator ruled the entire land.",
    "The teacher trained the class in rhetoric.",
    "The old cardinal blessed the cathedral altar.",

    # 20 sentences (emphasizing C and S)
    "The sacred scholar.. studied the secret code.",
    "A classic castle. stood in the cold district.",
    "The stern critic scored the classic article.",
    "Children sang. the sacred. choral score.",
    "The saint. created the sacred doctrine.",
    "Rich scholars. collected the ancient scripts.",
    "The clever cat chased. the scared rat.",
    "A harsh crisis scared. the entire class.",
    "The cardinal. blessed the sacred cathedral.",
    "She solved the secret code in silence.",
    "The scholar searched. the ancient scrolls.",
    "Cold winds. crossed. the. silent desert.",
    "The director cast the. actors in silence.",
    "Success came to the. skilled sailor.",
    "The critic. scorned the. classic stories.",
    "Children scattered. the. colored stones.",
    "The. chancellor. signed. the. secret . contract.",
    "A sacred.chant.echoed. in the cathedral.",
    "The scholar discussed the classic doctrine.",
    "The stern captain controlled the ship course.",
    
    "ion in onion is not noo",
    "isidor does not do onion ions",
    "is an onion cool or colored",
    "cool coon or scared onion",
    "dood is not cool",
    
    "A cold hard coral land has old sand and solid chains.",
    "Local radicals call on a hard social class.",
    "Old cars and hard roads cross cold land.",
    "A scholar holds cold alcohol and soda.",
    "Hard rain drains on solid local roads.",
    "Coral chains and sand on old hard land.",
    "A radical calls on social and local class.",
    "Old solid chairs on cold hard land.",
    "Hair on a cold hard chair.",
    "Oral class on local school and social hall.",
    "Dollar cards and hard cash on solid land.",
    "Radar and sonar on hard cold land.",
    "Canal and road on local hard land.",
    "Classical local scholar holds old solid scroll.",
    "Across cold hard land a radical car rolls.",
    "Acid and soda on hard cold coral.",
    "Halo and air on solid old land.",
    "Collar and chain on hard old chair.",
    "A solar hard cold local land has coral.",
    "Hard cold local radicals call solid old cars.",


    
]

Extras = [


    
    "The.sacred.scholar...studied.the.secret.code.",
    "A.classic.castle..stood.in.the.cold.district.",
    "The.stern.critic.scored.the.classic.article.",
    "Children.sang..the.sacred..choral.score.",
    "The.saint..created.the.sacred.doctrine.",
    "Rich.scholars..collected.the.ancient.scripts.",
    "The.clever.cat.chased..the.scared.rat.",
    "A.harsh.crisis.scared..the.entire.class.",
    "The.cardinal..blessed.the.sacred.cathedral.",
    "She.solved.the.secret.code.in.silence.",
    "The.scholar.searched..the.ancient.scrolls.",
    "Cold.winds..crossed..the..silent.desert.",
    "The.director.cast.the..actors.in.silence.",
    "Success.came.to.the..skilled.sailor.",
    "The.critic..scorned.the..classic.stories.",
    "Children.scattered..the..colored.stones.",
    "The..chancellor..signed..the..secret...contract.",
    "A.sacred.chant.echoed..in.the.cathedral.",
    "The.scholar.discussed.the.classic.doctrine.",
    "The.stern.captain.controlled.the.ship.course.",
    
    "asa sallolo solo sad sac.",
    "dance or danssa a a aaaa o o o no one noes.",
    "nits oo saa nii.",
    "lasa salla alosono lassa raa.",
    "dala rala salalo.",
    "ino ino ion no ion no",
    

]

if IncludeForcedLetters:
    sentences.extend(Extras)

# Verify they are clean (for your safety)
allowed = set("ACDEHILNORST acdehilnoprst.,")
for s in sentences:
    if not all(c in allowed for c in s):
        print("Warning: Invalid characters in sentence!")

print("Typing Capture Program")
print("Type the sentence exactly as shown.")
print("Press Ctrl+C to quit.\n")

try:
    total_chars = 0
    while True:
        # Pick random sentence
        sentence = random.choice(sentences).lower()
        if not all(c in allowed for c in sentence):
            continue
        print(f"\nTotal: {total_chars} -- Type:\n\n{sentence}\n")
        
        # Start timer
        start_time = time.time()
        
        # Get user input
        typed = input("Your typing: ")
        
        # End timer
        end_time = time.time()
        duration = end_time - start_time
        
        # Calculate stats
        char_count = len(sentence)          # includes spaces and period
        letters_only = len([c for c in sentence if c.isalpha()])
        total_chars += len(typed)
        if duration > 0:
            chars_per_second = char_count / duration
            avg_time_per_char = duration / char_count
        else:
            chars_per_second = 0
            avg_time_per_char = 0
        
        # Show results
        print(f"\nTime taken: {duration:.2f} seconds")
        print(f"Characters per second (incl. spaces): {chars_per_second:.2f}")
        print(f"Average time per character: {avg_time_per_char*1000:.1f} ms")
        
        if typed.strip() == sentence.strip():
            print("Exact match.")
        else:
            print("Not an exact match.")
        print(f"Total chars now: {total_chars}")
        print("-" * 50)
        
except KeyboardInterrupt:
    print("\n\nGood job, sir.")
    sys.exit(0)
