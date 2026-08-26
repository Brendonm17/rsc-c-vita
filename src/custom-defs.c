// custom item, NPC, and object definitions overlaid onto the base game data
#include "custom-defs.h"
#include "game-data.h"
#include "surface.h"
#include "utility.h"
// GL_CUSTOM_ENTITY_* and gl_custom_entity_anims[] come from surface.h's custom-entities.h include
#include <stdlib.h>
#include <string.h>

#define CUSTOM_ITEM_BASE 1290
#define CUSTOM_ITEM_COUNT 302

static const struct { const char *name; const char *description; const char *command;
    unsigned short sprite; unsigned int price; unsigned char stackable; unsigned char special;
    unsigned char members; } CUSTOM_ITEMS[CUSTOM_ITEM_COUNT] = {
    {"Vial of Water", "It's full of water", "", 450, 2, 0, 0, 0},
    {"Perfect gold", "this needs refining", "", 451, 150, 0, 1, 1},
    {"Perfect gold bar", "this looks valuable", "", 452, 300, 0, 1, 1},
    {"Perfect Ruby ring", "A valuable ring", "", 453, 2025, 0, 1, 1},
    {"Perfect Ruby necklace", "I wonder if this is valuable", "", 454, 2175, 0, 1, 1},
    {"Drunk dragon (Player-mixed)", "A warm creamy alcoholic beverage", "drink", 455, 2, 0, 0, 1},
    {"Ironman helm", "For just a rather very independent scaper.", "", 456, 154, 0, 1, 0},
    {"Ironman platebody", "Take it off and what are you?", "", 457, 560, 0, 1, 0},
    {"Ironman platelegs", "Take it off and what are you?", "", 458, 280, 0, 1, 0},
    {"Ultimate ironman helm", "For Just A Rather Very Independent Scaper.", "", 459, 154, 0, 1, 0},
    {"Ultimate ironman platebody", "Take it off and what are you?", "", 460, 560, 0, 1, 0},
    {"Ultimate ironman platelegs", "Take it off and what are you?", "", 461, 280, 0, 1, 0},
    {"Hardcore ironman helm", "For those who stand alone.", "", 462, 154, 0, 1, 0},
    {"Hardcore ironman platebody", "Take it off and what are you?", "", 463, 560, 0, 1, 0},
    {"Hardcore ironman platelegs", "Take it off and what are you?", "", 464, 280, 0, 1, 0},
    {"Rune stone", "An uncharged runestone", "", 465, 4, 0, 0, 0},
    {"Air talisman", "A mysterious power emanates from the talisman...", "Locate", 466, 40, 0, 0, 0},
    {"Mind talisman", "A mysterious power emanates from the talisman...", "Locate", 467, 30, 0, 0, 0},
    {"Water talisman", "A mysterious power emanates from the talisman...", "Locate", 468, 40, 0, 0, 0},
    {"Earth talisman", "A mysterious power emanates from the talisman...", "Locate", 469, 40, 0, 0, 0},
    {"Fire talisman", "A mysterious power emanates from the talisman...", "Locate", 470, 40, 0, 0, 0},
    {"Body talisman", "A mysterious power emanates from the talisman...", "Locate", 471, 30, 0, 0, 0},
    {"Cosmic talisman", "A mysterious power emanates from the talisman...", "Locate", 472, 150, 0, 0, 0},
    {"Chaos talisman", "A mysterious power emanates from the talisman...", "Locate", 473, 100, 0, 0, 0},
    {"Nature talisman", "A mysterious power emanates from the talisman...", "Locate", 474, 70, 0, 0, 0},
    {"Law talisman", "A mysterious power emanates from the talisman...", "Locate", 475, 120, 0, 0, 0},
    {"Death talisman", "A mysterious power emanates from the talisman...", "Locate", 476, 200, 0, 0, 0},
    {"Blood talisman", "A mysterious power emanates from the talisman...", "Locate", 477, 250, 0, 0, 0},
    {"Research package", "This contains some vital research results.", "", 478, 0, 0, 1, 1},
    {"Research notes", "These make no sense at all.", "", 479, 0, 0, 1, 1},
    {"Ring of recoil", "An enchanted ring.", "Check,Break", 480, 900, 0, 0, 1},
    {"Ring of splendor", "An enchanted ring.", "", 481, 1275, 0, 0, 1},
    {"Ring of forging", "An enchanted ring.", "Check,Break", 482, 2025, 0, 0, 1},
    {"Ring of life", "An enchanted ring.", "", 483, 3525, 0, 0, 1},
    {"Ring of wealth", "An enchanted ring.", "", 484, 17625, 0, 0, 1},
    {"Ring of avarice", "An enchanted ring.", "", 485, 17625, 0, 0, 1},
    {"Dwarven ring", "An enchanted ring.", "Check,Break", 486, 400, 0, 0, 1},
    {"Opal ring", "A valuable ring", "", 487, 1050, 0, 0, 0},
    {"White wolf mask", "Awoooo", "", 488, 1, 0, 0, 0},
    {"Blood wolf mask", "Awoooo", "", 489, 1, 0, 0, 0},
    {"Black wolf mask", "Awoooo", "", 490, 1, 0, 0, 0},
    {"Pink wolf mask", "Awoooo", "", 491, 1, 0, 0, 0},
    {"White unicorn mask", "I'm so fluffy I'm gonne die!!", "", 492, 1, 0, 0, 0},
    {"Blood unicorn mask", "I'm so fluffy I'm gonne die!!", "", 493, 1, 0, 0, 0},
    {"Black unicorn mask", "I'm so fluffy I'm gonne die!!", "", 494, 1, 0, 0, 0},
    {"Pink unicorn mask", "I'm so fluffy I'm gonne die!!", "", 495, 1, 0, 0, 0},
    {"Trick or treat cracker", "Use on another player to pull it", "", 496, 0, 0, 0, 0},
    {"Fox mask", "Struttin' like a fox", "", 497, 1, 0, 0, 0},
    {"Christmas cape", "A cape worn on the holidays", "", 498, 3, 0, 0, 0},
    {"Santa's hat with beard", "It's a santa claus' hat with a beard!", "", 499, 160, 0, 0, 0},
    {"Christmas Apron", "An apron for the festivities", "", 500, 2, 0, 0, 0},
    {"Glass of milk", "A glass of tasty milk", "drink", 501, 2, 0, 0, 0},
    {"Cane cookie", "A tasty holiday cookie", "eat", 502, 2, 0, 0, 0},
    {"Star cookie", "A tasty holiday cookie", "eat", 503, 2, 0, 0, 0},
    {"Tree cookie", "A tasty holiday cookie", "eat", 504, 2, 0, 0, 0},
    {"Santa's Gloves", "These keep Santa's hands warm", "", 505, 6, 0, 0, 0},
    {"Santa's Mittens", "Santa's favorite mittens", "", 506, 6, 0, 0, 0},
    {"Santa's suit", "A suit full of joy", "", 507, 8, 0, 0, 0},
    {"Santa's suit", "A suit full of joy", "", 508, 8, 0, 0, 0},
    {"Antlers with red-nose", "Im Rudolph the reindeer!!!", "", 509, 3, 0, 0, 0},
    {"Beverage glass", "A glass left after a tasty drink", "", 510, 1, 0, 0, 0},
    {"Dragon 2-handed Sword", "A massive sword", "", 511, 5000000, 0, 0, 0},
    {"King Black Dragon scale", "Taken from a monstrous beast", "", 512, 2500, 0, 0, 1},
    {"red apple", "Seems tasty!", "eat", 513, 1, 0, 0, 0},
    {"grapefruit", "It's very fresh", "eat", 514, 2, 0, 0, 1},
    {"papaya", "Seems very tasty!", "eat", 515, 2, 0, 0, 1},
    {"coconut", "It can be cut up with a machette", "", 516, 2, 0, 0, 1},
    {"Red Cabbage", "Yuck I don't like cabbage", "Eat", 517, 1, 0, 0, 0},
    {"Corn", "Some fresh picked corn", "eat", 518, 2, 0, 0, 0},
    {"White Pumpkin", "Wonder how it tastes", "eat", 519, 2, 0, 0, 1},
    {"Fruit Picker", "Useful for picking trees better", "", 520, 10, 0, 0, 0},
    {"Hand Shovel", "This will help get yield from bushes and allotments", "", 521, 15, 0, 0, 0},
    {"Herb Clippers", "Useful for picking up herbs out there", "", 522, 25, 0, 0, 1},
    {"Watering Can", "It's a watering can", "", 523, 20, 0, 0, 0},
    {"grapefruit slices", "It's very fresh", "eat", 524, 2, 0, 0, 1},
    {"Diced grapefruit", "Fresh chunks of grapefruit", "eat", 525, 2, 0, 0, 1},
    {"Half coconut", "Looks like some great coconut", "", 526, 2, 0, 0, 1},
    {"Teddy body", "A fluffy teddy body", "", 527, 1, 0, 1, 0},
    {"Teddy head", "A fluffy teddy head", "", 528, 1, 0, 1, 0},
    {"Teddy", "A fluffy teddy", "", 529, 1, 0, 1, 0},
    {"Dragon bar", "it's a bar of dragon metal", "", 530, 100000, 0, 0, 1},
    {"Chipped Dragon Scale", "A piece of dragon scale", "", 531, 50, 1, 0, 1},
    {"Dragon Metal Chain", "Linked dragon loops", "", 532, 2000, 1, 0, 1},
    {"Dragon Scale Mail Body", "A dragon chain mail reinforced with dragon scales", "", 533, 1500000, 0, 0, 0},
    {"Dwarf Smithy Note", "Details how to make the Dragon Scale Mail", "read", 534, 1, 0, 1, 1},
    {"Leather chaps", "They seem like decent protection", "", 535, 14, 0, 0, 0},
    {"Leather top", "Stylish leather top", "", 536, 21, 0, 0, 0},
    {"Leather skirt", "A ladies skirt made of leather", "", 537, 14, 0, 0, 0},
    {"Cooking cape", "The cape worn by the world's best chefs", "", 538, 99000, 0, 1, 0},
    {"Attack cape", "The cape worn by masters of attack", "", 539, 99000, 0, 1, 0},
    {"Thieving cape", "The cape worn by masters of thieving", "", 540, 99000, 0, 0, 0},
    {"Fletching cape", "The cape worn by masters of fletching", "", 541, 99000, 0, 0, 0},
    {"Mining cape", "The cape worn by masters of mining", "", 542, 99000, 0, 0, 0},
    {"Pestilence Mask", "You wouldn't want to be seen in this! Stay the cabbage home!", "", 543, 1, 0, 0, 0},
    {"Rubber Chicken Cap", "Wow. That was some very in-depth research on the 'chicken or the egg' question.", "", 544, 1, 0, 0, 0},
    {"Fishing cape", "The cape worn by the best fishermen", "Teleport", 545, 99000, 0, 0, 0},
    {"Strength cape", "The cape worn by only the strongest people", "", 546, 99000, 0, 0, 0},
    {"Magic cape", "The cape worn by the most powerful mages", "", 547, 99000, 0, 0, 0},
    {"Smithing cape", "The cape worn by master smiths", "", 548, 99000, 0, 0, 0},
    {"Crafting cape", "The cape worn by master craftworkers", "Teleport", 549, 99000, 0, 0, 0},
    {"Uncharged talisman", "This needs charging to work properly...", "", 550, 4, 0, 0, 0},
    {"Cursed air talisman", "A mysterious power emanates from the talisman...", "Locate", 551, 0, 0, 1, 0},
    {"Cursed mind talisman", "A mysterious power emanates from the talisman...", "Locate", 552, 0, 0, 1, 0},
    {"Cursed water talisman", "A mysterious power emanates from the talisman...", "Locate", 553, 0, 0, 1, 0},
    {"Cursed earth talisman", "A mysterious power emanates from the talisman...", "Locate", 554, 0, 0, 1, 0},
    {"Cursed fire talisman", "A mysterious power emanates from the talisman...", "Locate", 555, 0, 0, 1, 0},
    {"Cursed body talisman", "A mysterious power emanates from the talisman...", "Locate", 556, 0, 0, 1, 0},
    {"Cursed cosmic talisman", "A mysterious power emanates from the talisman...", "Locate", 557, 0, 0, 1, 0},
    {"Cursed chaos talisman", "A mysterious power emanates from the talisman...", "Locate", 558, 0, 0, 1, 0},
    {"Cursed nature talisman", "A mysterious power emanates from the talisman...", "Locate", 559, 0, 0, 1, 0},
    {"Cursed law talisman", "A mysterious power emanates from the talisman...", "Locate", 560, 0, 0, 1, 0},
    {"Cursed death talisman", "A mysterious power emanates from the talisman...", "Locate", 561, 0, 0, 1, 0},
    {"Cursed blood talisman", "A mysterious power emanates from the talisman...", "Locate", 562, 0, 0, 1, 0},
    {"Enfeebled air talisman", "A mysterious power emanates from the talisman...", "Locate", 563, 0, 0, 1, 0},
    {"Enfeebled mind talisman", "A mysterious power emanates from the talisman...", "Locate", 564, 0, 0, 1, 0},
    {"Enfeebled water talisman", "A mysterious power emanates from the talisman...", "Locate", 565, 0, 0, 1, 0},
    {"Enfeebled earth talisman", "A mysterious power emanates from the talisman...", "Locate", 566, 0, 0, 1, 0},
    {"Enfeebled fire talisman", "A mysterious power emanates from the talisman...", "Locate", 567, 0, 0, 1, 0},
    {"Enfeebled body talisman", "A mysterious power emanates from the talisman...", "Locate", 568, 0, 0, 1, 0},
    {"Enfeebled cosmic talisman", "A mysterious power emanates from the talisman...", "Locate", 569, 0, 0, 1, 0},
    {"Enfeebled chaos talisman", "A mysterious power emanates from the talisman...", "Locate", 570, 0, 0, 1, 0},
    {"Enfeebled nature talisman", "A mysterious power emanates from the talisman...", "Locate", 571, 0, 0, 1, 0},
    {"Enfeebled law talisman", "A mysterious power emanates from the talisman...", "Locate", 572, 0, 0, 1, 0},
    {"Enfeebled death talisman", "A mysterious power emanates from the talisman...", "Locate", 573, 0, 0, 1, 0},
    {"Enfeebled blood talisman", "A mysterious power emanates from the talisman...", "Locate", 574, 0, 0, 1, 0},
    {"Fish oil", "Good for my heart", "Eat", 575, 1, 1, 0, 0},
    {"Runecraft Potion", "3 doses of runecraft potion", "Drink", 576, 200, 0, 0, 1},
    {"Runecraft Potion", "2 doses of runecraft potion", "Drink", 577, 150, 0, 0, 1},
    {"Runecraft Potion", "1 dose of runecraft potion", "Drink", 578, 100, 0, 0, 1},
    {"Super Runecraft Potion", "3 doses of super runecraft potion", "Drink", 579, 400, 0, 0, 1},
    {"Super Runecraft Potion", "2 doses of super runecraft potion", "Drink", 580, 300, 0, 0, 1},
    {"Super Runecraft Potion", "1 dose of super runecraft potion", "Drink", 581, 200, 0, 0, 1},
    {"Pizza Bagel", "I sure wish I could make these on my own", "Eat", 582, 50, 0, 0, 0},
    {"Bronze Chain Mail Legs", "A series of connected metal rings", "", 583, 30, 0, 0, 0},
    {"Iron Chain Mail Legs", "A series of connected metal rings", "", 584, 105, 0, 0, 0},
    {"Steel Chain Mail Legs", "A series of connected metal rings", "", 585, 375, 0, 0, 0},
    {"Mithril Chain Mail Legs", "A series of connected metal rings", "", 586, 975, 0, 0, 0},
    {"Adamantite Chain Mail Legs", "A series of connected metal rings", "", 587, 2400, 0, 0, 0},
    {"Rune Chain Mail Legs", "A series of connected metal rings", "", 588, 37500, 0, 0, 0},
    {"Black Chain Mail Legs", "A series of connected metal rings", "", 589, 720, 0, 0, 0},
    {"Large Dragon Helmet", "A full face helmet", "", 590, 5000000, 0, 0, 0},
    {"Dragon Kite Shield", "An ancient and powerful looking Dragon Kite shield", "", 591, 5000000, 0, 0, 0},
    {"Dragon Plate Mail Body", "Provides excellent protection", "", 592, 5000000, 0, 0, 0},
    {"Dragon Plate Mail Top", "Armour designed for females", "", 593, 5000000, 0, 0, 0},
    {"Dragon Plate Mail Legs", "These look pretty heavy", "", 594, 5000000, 0, 0, 0},
    {"Dragon Plated Skirt", "Designer leg protection", "", 595, 5000000, 0, 0, 0},
    {"White CTF Flag", "White Capture the flag banner", "", 596, 1, 0, 0, 0},
    {"Guthix CTF Flag", "Guthix capture the flag banner", "", 597, 1, 0, 0, 0},
    {"Saradomin CTF Flag", "Saradomin capture the flag banner", "", 598, 1, 0, 0, 0},
    {"Zamorak CTF Flag", "Zamorak capture the flag banner", "", 599, 1, 0, 0, 0},
    {"White Wings", "White Wings", "", 600, 0, 0, 0, 0},
    {"Medium Valkyrie Helmet", "A medium sized Valkyrie helmet", "", 601, 1, 0, 0, 0},
    {"Medium Guthix Valkyrie Helmet", "A medium sized Guthix Valkyrie helmet", "", 602, 0, 0, 0, 0},
    {"Medium Saradomin Valkyrie Helmet", "A medium sized Saradomin Valkyrie helmet", "", 603, 0, 0, 0, 0},
    {"Medium Zamorak Valkyrie Helmet", "A medium sized Zamorak Valkyrie helmet", "", 604, 0, 0, 0, 0},
    {"Large Valkyrie Helmet", "A large sized Valkyrie helmet", "", 605, 0, 0, 0, 0},
    {"Large Guthix Valkyrie Helmet", "A large sized Guthix Valkyrie helmet", "", 606, 0, 0, 0, 0},
    {"Large Saradomin Valkyrie Helmet", "A large sized Saradomin Valkyrie helmet", "", 607, 0, 0, 0, 0},
    {"Large Zamorak Valkyrie Helmet", "A large sized Zamorak Valkyrie helmet", "", 608, 0, 0, 0, 0},
    {"Guthix Wings", "Guthix Wings", "", 609, 0, 0, 0, 0},
    {"Saradomin Wings", "Saradomin Wings", "", 610, 0, 0, 0, 0},
    {"Zamorak Wings", "Zamorak Wings", "", 611, 0, 0, 0, 0},
    {"Dragon dagger", "Short but pointy", "", 612, 200000, 0, 0, 0},
    {"Poisoned dragon dagger", "Short but pointy", "", 613, 300000, 0, 0, 1},
    {"Dragon arrows", "Large arrows for the dragon longbow", "", 614, 3000, 1, 0, 1},
    {"Poison dragon arrows", "Venomous large arrows for the dragon longbow", "", 615, 3000, 1, 0, 1},
    {"Dragon bolts", "Great if you have a dragon crossbow!", "", 616, 3000, 1, 0, 1},
    {"Poison dragon bolts", "Good if you have a dragon crossbow!", "", 617, 3000, 1, 0, 1},
    {"Dragon crossbow", "This fires crossbow bolts", "", 618, 300000, 0, 0, 1},
    {"Dragon longbow", "A nice sturdy bow", "", 619, 300000, 0, 0, 1},
    {"Watering Can", "It's an empty watering can", "", 620, 20, 0, 0, 0},
    {"sugar cane", "These can sweeten things up", "", 621, 2, 0, 0, 1},
    {"dragonfruit", "A powerful fruit", "", 622, 3, 0, 0, 1},
    {"sliced dragonfruit", "Some great dragonfruit ready to be used", "", 623, 3, 0, 0, 1},
    {"Sweetened Slices", "Slices of fruit both sweet and sour", "eat", 624, 2, 1, 0, 1},
    {"Sweetened Chunks", "Chunks of fruit both sweet and sour", "eat", 625, 2, 1, 0, 1},
    {"Mixing bowl", "For mixing advanced cooking ingredients", "pour", 626, 2, 0, 0, 1},
    {"Uncooked seaweed soup", "I need to cook this", "", 627, 15, 0, 0, 1},
    {"Seaweed soup", "It's a seaweed soup", "Eat", 628, 25, 0, 0, 1},
    {"Burnt seaweed soup", "Eew it's horribly burnt", "", 629, 1, 0, 0, 1},
    {"grapes of Saradomin", "Strong grapes for a powerful wine", "", 630, 1, 0, 0, 1},
    {"grapes of Zamorak", "Strong grapes for a powerful wine", "", 631, 1, 0, 0, 1},
    {"wine of Saradomin", "It's full of wine", "Drink", 632, 1, 0, 0, 1},
    {"magic Potion", "3 doses of magic potion", "Drink", 633, 288, 0, 0, 1},
    {"magic Potion", "2 doses of magic potion", "Drink", 634, 216, 0, 0, 1},
    {"magic Potion", "1 dose of magic potion", "Drink", 635, 144, 0, 0, 1},
    {"Potion of Saradomin", "It looks dauntless", "drink", 636, 25, 0, 0, 1},
    {"Potion of Saradomin", "It looks dauntless", "drink", 637, 25, 0, 0, 1},
    {"Potion of Saradomin", "It looks dauntless", "drink", 638, 25, 0, 0, 1},
    {"Super ranging Potion", "3 doses of ranging potion", "Drink", 639, 288, 0, 0, 1},
    {"Super ranging Potion", "2 doses of ranging potion", "Drink", 640, 216, 0, 0, 1},
    {"Super ranging Potion", "1 dose of ranging potion", "Drink", 641, 144, 0, 0, 1},
    {"Super magic Potion", "3 doses of magic potion", "Drink", 642, 288, 0, 0, 1},
    {"Super magic Potion", "2 doses of magic potion", "Drink", 643, 216, 0, 0, 1},
    {"Super magic Potion", "1 dose of magic potion", "Drink", 644, 144, 0, 0, 1},
    {"Rabbit's Foot", "I do feel lucky, punk", "", 645, 0, 0, 1, 0},
    {"Rabbit's Foot", "I do feel lucky, punk", "", 646, 0, 0, 1, 0},
    {"Rabbit's Foot", "I do feel lucky, punk", "", 647, 0, 0, 1, 0},
    {"Rabbit's Foot", "I do feel lucky, punk", "", 648, 0, 0, 1, 0},
    {"Rabbit's Foot", "I do feel lucky, punk", "", 649, 0, 0, 1, 0},
    {"Ring of Bunny", "Imbued with the power of cuteness", "", 650, 0, 0, 1, 0},
    {"Ring of Egg", "Imbued with egg-streme power", "", 651, 0, 0, 1, 0},
    {"Unused", "Do Not Use", "", 652, 1, 0, 0, 0},
    {"Uncooked pumpkin pie", "I need to cook this first", "", 653, 1, 0, 0, 0},
    {"Pumpkin pie", "A festive autumn pie. It's rare to have a pie this nice.", "eat", 654, 30, 0, 0, 0},
    {"Half a pumpkin pie", "A festive autumn pie. It's rare to have a pie this nice.", "eat", 655, 10, 0, 0, 0},
    {"Uncooked white pumpkin pie", "I need to cook this first", "", 656, 1, 0, 0, 0},
    {"White pumpkin pie", "A festive autumn pie. It's weird that it's white.", "eat", 657, 30, 0, 0, 0},
    {"Half a white pumpkin pie", "A festive autumn pie. It's weird that it's white.", "eat", 658, 10, 0, 0, 0},
    {"Eak the Mouse", "A cute mouse", "Talk", 659, 1, 0, 1, 0},
    {"Yoyo", "This technology shouldn't be possible!", "Play", 660, 100, 0, 1, 0},
    {"Ogre Ears", "The ogres in Gu'Tannoth don't have ears like this...", "", 661, 100, 0, 1, 0},
    {"Leather vest", "It's kind of fashionable?", "", 662, 15, 0, 0, 0},
    {"Makeover Waiver", "yada yada yada...", "Read", 663, 15, 0, 1, 0},
    {"Soft Yellowgreen Clay", "I hope this colour doesn't get on my clothes", "Shape", 664, 2, 0, 1, 0},
    {"Ogre recipes", "Just like grandma used to make", "read", 665, 1, 0, 1, 0},
    {"Crown mould", "Used to make gold crowns", "", 666, 5, 0, 0, 0},
    {"Gold Crown", "I wonder what an enchantment would do on this valuable", "", 667, 550, 0, 0, 0},
    {"Sapphire Crown", "I wonder what an enchantment would do on this valuable", "", 668, 1200, 0, 0, 0},
    {"Emerald Crown", "I wonder what an enchantment would do on this valuable", "", 669, 1575, 0, 0, 0},
    {"Ruby Crown", "I wonder what an enchantment would do on this valuable", "", 670, 2325, 0, 0, 0},
    {"Diamond Crown", "I wonder what an enchantment would do on this valuable", "", 671, 3825, 0, 0, 0},
    {"Dragonstone Crown", "I wonder what an enchantment would do on this valuable", "", 672, 19125, 0, 0, 1},
    {"Crown of dew", "It gives me a humidifier sense", "Check,Break,Configure", 673, 1200, 0, 0, 0},
    {"Crown of mimicry", "It helps me avoid monsters when skilling", "Check,Break", 674, 1575, 0, 0, 0},
    {"Crown of the artisan", "It assists my skilling experience", "Check,Break", 675, 2325, 0, 0, 0},
    {"Crown of the items", "It brings forth an item on the ground when skilling", "Check,Break", 676, 3825, 0, 0, 0},
    {"Crown of the herbalist", "It gives me a sense to be one with herbs", "Check,Break,Configure", 677, 19125, 0, 0, 1},
    {"Crown of the occult", "It gives me a sense to be one with bones", "Check,Break,Configure", 678, 19125, 0, 0, 1},
    {"Cape of Inclusion", "A colourful cape made from many different pieces of cloth.", "", 679, 3, 0, 1, 0},
    {"Agility cape", "The cape worn by the most agile", "Teleport", 680, 99000, 0, 0, 0},
    {"Defense cape", "The cape worn by the most formidable", "", 681, 99000, 0, 0, 0},
    {"Firemaking cape", "The cape worn by pyro enthusiasts", "Combust", 682, 99000, 0, 0, 0},
    {"Herblaw cape", "The cape worn by master herblawists", "", 683, 99000, 0, 0, 0},
    {"Hits cape", "The cape worn by the most sturdy", "", 684, 99000, 0, 0, 0},
    {"Prayer cape", "The cape worn by the most pious", "", 685, 99000, 0, 0, 0},
    {"Ranged cape", "The cape worn by the best archers", "", 686, 99000, 0, 0, 0},
    {"Woodcutting cape", "The cape worn by the best loggers", "", 687, 99000, 0, 0, 0},
    {"Harvesting cape", "The cape worn by agronomists", "", 688, 99000, 0, 0, 0},
    {"Runecraft cape", "The cape worn by masters of rune lore", "", 689, 99000, 0, 0, 0},
    {"Quest cape", "The cape worn by the most seasoned adventurers", "", 690, 99000, 0, 0, 0},
    {"Max cape", "The cape worn by ???", "", 691, 99000, 0, 0, 0},
    {"Bronze Chain Mail Top", "A series of connected metal rings", "", 692, 60, 0, 0, 0},
    {"Iron Chain Mail Top", "A series of connected metal rings", "", 693, 210, 0, 0, 0},
    {"Steel Chain Mail Top", "A series of connected metal rings", "", 694, 750, 0, 0, 0},
    {"Black Chain Mail Top", "A series of connected metal rings", "", 695, 1440, 0, 0, 0},
    {"Mithril Chain Mail Top", "A series of connected metal rings", "", 696, 1950, 0, 0, 0},
    {"Adamantite Chain Mail Top", "A series of connected metal rings", "", 697, 4800, 0, 0, 0},
    {"Rune Chain Mail Top", "A series of connected metal rings", "", 698, 50000, 0, 0, 0},
    {"Dragon Scale Mail Top", "A dragon chain mail reinforced with dragon scales", "", 699, 1500000, 0, 0, 1},
    {"Animal fat", "Thick and gelatinous", "", 700, 0, 0, 0, 0},
    {"Treated hide", "I should use this on a fire to dry it", "", 701, 1, 0, 0, 0},
    {"lean bear meat", "I need to cook this first", "", 702, 1, 0, 0, 0},
    {"lean rat meat", "I need to cook this first", "", 703, 1, 0, 0, 0},
    {"lean beef", "I need to cook this first", "", 704, 1, 0, 0, 0},
    {"Rune stone certificate", "Each certificate exchangable at Varrock for 5 rune stone", "", 705, 10, 1, 0, 0},
    {"Unobtanium", "I should update my client.", "", 706, 17, 0, 0, 0},
    {"Unobtanium", "I should update my client.", "", 707, 17, 1, 0, 0},
    {"stat restoration Potion certificate", "Each certificate exchangable at Varrock for 5 stat restore potions", "", 708, 10, 1, 0, 0},
    {"giant carp certificate", "Each certificate exchangable at Varrock for 5 giant carp", "", 709, 10, 1, 0, 0},
    {"Lava eel certificate", "Each certificate exchangable at Varrock for 5 lava eels", "", 710, 10, 1, 0, 0},
    {"Poison antidote certificate", "Each certificate exchangable at Varrock for 5 poison antidote potions", "", 711, 10, 1, 0, 0},
    {"Manta ray certificate", "Each certificate exchangable at Varrock for 5 manta rays", "", 712, 10, 1, 0, 0},
    {"Sea turtle certificate", "Each certificate exchangable at Varrock for 5 sea turtles", "", 713, 10, 1, 0, 0},
    {"Cure poison Potion certificate", "Each certificate exchangable at Varrock for 5 cure poison potions", "", 714, 10, 1, 0, 0},
    {"Biggum Flodrot", "Biggum Flodrot, goblin hero", "Talk", 715, 0, 0, 1, 1},
    {"Ironman plate top", "Take it off and what are you?", "", 716, 560, 0, 1, 0},
    {"Ultimate ironman plate top", "Take it off and what are you?", "", 717, 560, 0, 1, 0},
    {"Hardcore ironman plate top", "Take it off and what are you?", "", 718, 560, 0, 1, 0},
    {"Ironman plated skirt", "Take it off and what are you?", "", 719, 280, 0, 1, 0},
    {"Ultimate ironman plated skirt", "Take it off and what are you?", "", 720, 280, 0, 1, 0},
    {"Hardcore ironman plated skirt", "Take it off and what are you?", "", 721, 280, 0, 1, 0},
    {"Bonecrusher", "A contraption that crushes bones to dust", "", 722, 0, 0, 1, 0},
    {"Chipped pestle and mortar", "The apothecary's old pestle & mortar", "", 723, 4, 0, 1, 0},
    {"aluminium bar", "this looks malleable", "", 724, 150, 0, 1, 0},
    {"aluminium cog", "A piece of machinery", "", 725, 150, 0, 1, 0},
    {"Wooden box", "A box made of wood", "", 726, 25, 0, 1, 0},
    {"Ring of Skull", "Imbued with the powers of a bonafide skeleton", "", 727, 0, 0, 1, 0},
    {"Spookie's Bones", "Better do something about these", "", 728, 1, 0, 1, 0},
    {"Scarie's Bones", "Better do something about these", "", 729, 1, 0, 1, 0},
    {"Lily's Pumpkin", "A pumpkin harvested from Lily's field", "eat", 730, 30, 0, 0, 0},
    {"Uncooked Lily's pumpkin pie", "I need to cook this first", "", 731, 1, 0, 0, 0},
    {"Lily's pumpkin pie", "Mmm a pie made with Lily's pumpkins", "eat", 732, 30, 0, 0, 0},
    {"Half a Lily's pumpkin pie", "Mmm a pie made with Lily's pumpkins", "eat", 733, 5, 0, 0, 0},
    {"Duke Horacio's Journal", "This is a journal not a diary", "read", 734, 1, 0, 1, 0},
    {"parchment", "I can write on this", "write", 735, 1, 0, 1, 0},
    {"Apology letter", "A heartfelt apology letter", "read", 736, 1, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 737, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 738, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 739, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 740, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 741, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 742, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 743, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 744, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 745, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 746, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 747, 5, 0, 1, 0},
    {"Christmas sweater", "Knitted with love!", "", 748, 5, 0, 1, 0},
    {"Necronomicon Ex Mortis", "This looks very homemade", "read", 749, 1, 0, 1, 0},
    {"Ancient amulet", "A sinister looking amulet", "", 750, 1, 0, 1, 0},
    {"Boomstick", "A 12-Gauge, Double-Barreled Remington", "", 751, 1, 0, 1, 0},
};

// worn-equipment slot bitmask per custom item, indexed by (client id - 1290); 0 = non-wearable
static const unsigned short CUSTOM_ITEM_WEARABLE[CUSTOM_ITEM_COUNT] = {
    0u, 0u, 0u, 0u, 0u, 0u, 33u, 322u, 644u, 33u, 322u, 644u,
    33u, 322u, 644u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 32u, 32u, 32u, 32u, 32u, 32u, 32u, 32u, 0u, 32u,
    2048u, 32u, 1024u, 0u, 0u, 0u, 0u, 256u, 256u, 64u, 64u, 32u,
    0u, 8216u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 64u,
    0u, 128u, 64u, 128u, 2048u, 2048u, 2048u, 2048u, 2048u, 32u, 32u, 2048u,
    2048u, 2048u, 2048u, 2048u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 128u, 128u, 128u, 128u, 128u, 128u, 128u, 33u, 8u, 322u, 322u,
    644u, 640u, 16u, 16u, 16u, 16u, 2048u, 32u, 32u, 32u, 32u, 32u,
    32u, 32u, 32u, 2048u, 2048u, 2048u, 16u, 16u, 0u, 0u, 0u, 0u,
    16u, 24u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 16u, 32u, 64u, 0u, 0u, 0u,
    0u, 32u, 32u, 32u, 32u, 32u, 32u, 32u, 32u, 32u, 32u, 32u,
    32u, 2048u, 2048u, 2048u, 2048u, 2048u, 2048u, 2048u, 2048u, 2048u, 2048u, 2048u,
    2048u, 2048u, 64u, 64u, 64u, 64u, 64u, 64u, 64u, 64u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 322u, 322u, 322u, 644u, 644u, 644u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 64u,
    64u, 64u, 64u, 64u, 64u, 64u, 64u, 64u, 64u, 64u, 64u, 0u,
    1024u, 16u,
};

#define CUSTOM_SPRITE_COUNT 302
static const struct { unsigned short slot; unsigned short w; unsigned short h; } CUSTOM_SPRITES[CUSTOM_SPRITE_COUNT] = {
    {450, 20, 32},
    {451, 18, 18},
    {452, 39, 16},
    {453, 19, 21},
    {454, 35, 21},
    {455, 19, 25},
    {456, 32, 16},
    {457, 39, 29},
    {458, 47, 21},
    {459, 32, 16},
    {460, 39, 29},
    {461, 47, 21},
    {462, 32, 16},
    {463, 39, 29},
    {464, 47, 21},
    {465, 28, 25},
    {466, 48, 32},
    {467, 48, 32},
    {468, 48, 32},
    {469, 48, 32},
    {470, 48, 32},
    {471, 48, 32},
    {472, 48, 32},
    {473, 48, 32},
    {474, 48, 32},
    {475, 48, 32},
    {476, 48, 32},
    {477, 48, 32},
    {478, 28, 32},
    {479, 33, 31},
    {480, 22, 25},
    {481, 22, 25},
    {482, 22, 25},
    {483, 22, 25},
    {484, 22, 25},
    {485, 20, 27},
    {486, 20, 27},
    {487, 19, 21},
    {488, 24, 20},
    {489, 24, 20},
    {490, 24, 20},
    {491, 24, 20},
    {492, 36, 20},
    {493, 36, 20},
    {494, 36, 20},
    {495, 36, 20},
    {496, 48, 32},
    {497, 24, 20},
    {498, 48, 32},
    {499, 37, 29},
    {500, 44, 24},
    {501, 17, 27},
    {502, 25, 21},
    {503, 24, 20},
    {504, 33, 18},
    {505, 24, 18},
    {506, 24, 18},
    {507, 41, 29},
    {508, 38, 21},
    {509, 37, 29},
    {510, 17, 27},
    {511, 48, 32},
    {512, 28, 23},
    {513, 25, 29},
    {514, 28, 25},
    {515, 26, 32},
    {516, 48, 32},
    {517, 31, 22},
    {518, 31, 23},
    {519, 27, 26},
    {520, 45, 28},
    {521, 28, 21},
    {522, 43, 19},
    {523, 24, 25},
    {524, 26, 18},
    {525, 23, 21},
    {526, 48, 32},
    {527, 28, 23},
    {528, 14, 14},
    {529, 28, 30},
    {530, 39, 16},
    {531, 19, 17},
    {532, 30, 28},
    {533, 33, 22},
    {534, 26, 27},
    {535, 33, 26},
    {536, 29, 20},
    {537, 28, 30},
    {538, 48, 32},
    {539, 48, 32},
    {540, 48, 32},
    {541, 48, 32},
    {542, 48, 32},
    {543, 45, 30},
    {544, 20, 31},
    {545, 48, 32},
    {546, 48, 32},
    {547, 48, 32},
    {548, 48, 32},
    {549, 48, 32},
    {550, 48, 32},
    {551, 48, 32},
    {552, 48, 32},
    {553, 48, 32},
    {554, 48, 32},
    {555, 48, 32},
    {556, 48, 32},
    {557, 48, 32},
    {558, 48, 32},
    {559, 48, 32},
    {560, 48, 32},
    {561, 48, 32},
    {562, 48, 32},
    {563, 48, 32},
    {564, 48, 32},
    {565, 48, 32},
    {566, 48, 32},
    {567, 48, 32},
    {568, 48, 32},
    {569, 48, 32},
    {570, 48, 32},
    {571, 48, 32},
    {572, 48, 32},
    {573, 48, 32},
    {574, 48, 32},
    {575, 20, 32},
    {576, 20, 32},
    {577, 20, 32},
    {578, 20, 32},
    {579, 20, 32},
    {580, 20, 32},
    {581, 20, 32},
    {582, 41, 29},
    {583, 33, 26},
    {584, 33, 26},
    {585, 33, 26},
    {586, 33, 26},
    {587, 33, 26},
    {588, 33, 26},
    {589, 33, 26},
    {590, 25, 16},
    {591, 39, 32},
    {592, 39, 29},
    {593, 38, 28},
    {594, 47, 21},
    {595, 38, 28},
    {596, 37, 26},
    {597, 37, 26},
    {598, 37, 26},
    {599, 37, 26},
    {600, 47, 29},
    {601, 25, 16},
    {602, 25, 16},
    {603, 25, 16},
    {604, 25, 16},
    {605, 25, 16},
    {606, 25, 16},
    {607, 25, 16},
    {608, 25, 16},
    {609, 47, 29},
    {610, 47, 29},
    {611, 47, 29},
    {612, 27, 16},
    {613, 27, 16},
    {614, 45, 18},
    {615, 45, 18},
    {616, 25, 22},
    {617, 25, 22},
    {618, 40, 27},
    {619, 40, 25},
    {620, 24, 25},
    {621, 28, 25},
    {622, 15, 31},
    {623, 15, 31},
    {624, 26, 18},
    {625, 23, 21},
    {626, 33, 28},
    {627, 33, 28},
    {628, 33, 28},
    {629, 33, 28},
    {630, 20, 22},
    {631, 20, 22},
    {632, 25, 25},
    {633, 20, 32},
    {634, 20, 32},
    {635, 20, 32},
    {636, 20, 32},
    {637, 20, 32},
    {638, 20, 32},
    {639, 20, 32},
    {640, 20, 32},
    {641, 20, 32},
    {642, 20, 32},
    {643, 20, 32},
    {644, 20, 32},
    {645, 41, 29},
    {646, 41, 29},
    {647, 41, 29},
    {648, 41, 29},
    {649, 41, 29},
    {650, 41, 29},
    {651, 41, 29},
    {652, 35, 22},
    {653, 37, 24},
    {654, 37, 24},
    {655, 37, 24},
    {656, 37, 24},
    {657, 37, 24},
    {658, 37, 24},
    {659, 48, 32},
    {660, 48, 32},
    {661, 48, 32},
    {662, 33, 25},
    {663, 33, 31},
    {664, 35, 20},
    {665, 26, 27},
    {666, 27, 27},
    {667, 25, 14},
    {668, 25, 14},
    {669, 25, 14},
    {670, 25, 14},
    {671, 25, 14},
    {672, 25, 14},
    {673, 25, 14},
    {674, 25, 14},
    {675, 25, 14},
    {676, 25, 14},
    {677, 25, 14},
    {678, 25, 14},
    {679, 44, 28},
    {680, 48, 32},
    {681, 48, 32},
    {682, 48, 32},
    {683, 48, 32},
    {684, 48, 32},
    {685, 48, 32},
    {686, 48, 32},
    {687, 48, 32},
    {688, 48, 32},
    {689, 48, 32},
    {690, 48, 32},
    {691, 48, 32},
    {692, 34, 18},
    {693, 34, 18},
    {694, 34, 18},
    {695, 34, 18},
    {696, 34, 18},
    {697, 34, 18},
    {698, 34, 18},
    {699, 33, 22},
    {700, 27, 17},
    {701, 48, 32},
    {702, 34, 22},
    {703, 34, 22},
    {704, 34, 22},
    {705, 30, 29},
    {706, 33, 23},
    {707, 33, 23},
    {708, 30, 29},
    {709, 30, 29},
    {710, 30, 29},
    {711, 30, 29},
    {712, 30, 29},
    {713, 30, 29},
    {714, 30, 29},
    {715, 48, 32},
    {716, 38, 28},
    {717, 38, 28},
    {718, 38, 28},
    {719, 38, 28},
    {720, 38, 28},
    {721, 38, 28},
    {722, 48, 32},
    {723, 36, 20},
    {724, 39, 16},
    {725, 48, 32},
    {726, 48, 32},
    {727, 19, 21},
    {728, 40, 28},
    {729, 40, 28},
    {730, 27, 26},
    {731, 37, 24},
    {732, 37, 24},
    {733, 37, 24},
    {734, 30, 32},
    {735, 42, 31},
    {736, 42, 31},
    {737, 40, 29},
    {738, 40, 29},
    {739, 40, 29},
    {740, 40, 29},
    {741, 40, 29},
    {742, 40, 29},
    {743, 40, 29},
    {744, 40, 29},
    {745, 40, 29},
    {746, 40, 29},
    {747, 40, 29},
    {748, 40, 29},
    {749, 30, 32},
    {750, 42, 23},
    {751, 41, 23},
};

#define CUSTOM_NPC_BASE 794
#define CUSTOM_NPC_COUNT 44
#define CUSTOM_NPC_CLONE_ID 24

// custom NPC real appearances: layered animation-sprite array, recolour channels, dims, speeds
static const struct {
    const char *name; const char *description; const char *command;
    unsigned char attack; unsigned char strength; unsigned char hits; unsigned char defense;
    short sprites[NPC_SPRITE_COUNT];
    unsigned short width; unsigned short height;
    unsigned char walk_speed; unsigned char combat_speed; unsigned char combat_width;
    unsigned int hair_colour; unsigned int top_colour;
    unsigned int bottom_colour; unsigned int skin_colour;
    unsigned char use_clone;
    } CUSTOM_NPCS[CUSTOM_NPC_COUNT] = {
    // ids are real OpenRSC wire ids (EntityHandler order)
    { "Gundai", "He must get lonely out here", "", 15, 16, 12, 18, { 6, 1, 2, -1, -1, -1, -1, -1, 46, -1, -1, -1 }, 145, 230, 6, 6, 5, 11167296u, 8409120u, 3u, 13415270u, 0 }, // 794 authentic-layered
    { "Lundail", "He sells rune stones", "", 15, 16, 12, 18, { 6, 1, 2, -1, -1, -1, -1, -1, 46, -1, -1, -1 }, 145, 230, 6, 6, 5, 11167296u, 8409120u, 3u, 13415270u, 0 }, // 795 authentic-layered
    { "Auctioneer", "He gives access to auction house", "Auction", 0, 0, 3, 0, { 0, 1, 2, -1, -1, -1, -1, -1, 46, -1, -1, -1 }, 145, 230, 6, 6, 5, 16761440u, 2u, 8409120u, 13415270u, 0 }, // 796 authentic-layered
    { "Auction Clerk", "There to help me make my auctions", "Auction", 15, 16, 12, 18, { 3, 4, 2, -1, -1, -1, -1, -1, -1, 11, -1, -1 }, 145, 220, 6, 6, 5, 11167296u, 11141375u, 11141375u, 14415270u, 0 }, // 797 authentic-layered
    { "Subscription Vendor", "Exchange your subscription token to subscription time", "", 0, 0, 3, 0, { 3, 4, 2, -1, -1, 77, -1, -1, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 16711680u, 143190u, 143190u, 15523536u, 0 }, // 798 authentic-layered
    { "Subscription Vendor", "Exchange your subscription token to subscription time", "", 0, 0, 3, 0, { 0, 1, 2, -1, -1, 77, -1, -1, -1, -1, -1, -1 }, 145, 230, 6, 6, 5, 16761440u, 143190u, 143190u, 15523536u, 0 }, // 799 authentic-layered
    { "Gaia", "The earth queen with a rotten heart", "", 78, 79, 79, 80, { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 275, 262, 11, 11, 30, 0u, 0u, 0u, 0u, 1 }, // 800 needs-custom-sprite
    { "Ironman", "An Ironman", "Armour", 0, 0, 0, 0, { 0, -1, -1, -1, -1, -1, 27, 36, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 6751590u, 0u, 14u, 13415270u, 0 }, // 801 authentic-layered
    { "Ultimate Ironman", "An Ultimate Ironman", "Armour", 0, 0, 0, 0, { 3, -1, -1, -1, -1, -1, 54, -1, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 11167296u, 8u, 14u, 13415270u, 0 }, // 802 needs-custom-sprite
    { "Hardcore Ironman", "A Hardcore Ironman", "Armour", 0, 0, 0, 0, { 0, -1, -1, -1, -1, 12, 27, 36, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 11167296u, 8u, 14u, 13415270u, 0 }, // 803 authentic-layered
    { "Greatwood", "A scary hard slamming tree", "", 255, 245, 400, 300, { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 345, 410, 11, 11, 30, 0u, 0u, 0u, 0u, 1 }, // 804 needs-custom-sprite
    { "Wizard Sedridor", "An old wizard", "", 0, 0, 0, 0, { 6, 1, 2, -1, -1, 77, 76, 81, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 16777215u, 255u, 255u, 15523536u, 0 }, // 805 authentic-layered
    { "Scot Ruth", "A smelly, dirty dwarf", "", 20, 17, 16, 20, { 6, 1, 2, -1, -1, -1, 45, -1, -1, -1, -1, 62 }, 121, 176, 6, 6, 5, 7360576u, 3158064u, 3158064u, 15523536u, 0 }, // 806 authentic-layered
    { "Gardener", "She takes care of the plants around", "shopOption", 25, 25, 10, 20, { 3, 4, 2, -1, -1, -1, -1, 81, -1, -1, -1, -1 }, 125, 225, 6, 6, 5, 16753488u, 5286432u, 10510400u, 13415270u, 0 }, // 807 authentic-layered
    { "Gramat", "He looks worried", "", 20, 17, 16, 20, { 6, 1, 2, -1, -1, -1, 45, -1, -1, -1, -1, -1 }, 121, 176, 6, 6, 5, 7360576u, 9465888u, 13393952u, 15523536u, 0 }, // 808 authentic-layered
    { "Dwarven Smithy", "A master of metals", "", 20, 17, 16, 20, { 6, 1, 2, -1, -1, -1, 45, -1, -1, -1, -1, -1 }, 121, 176, 6, 6, 5, 7360576u, 9465888u, 13393952u, 15523536u, 0 }, // 809 authentic-layered
    { "Dwarven Youth", "He is upset", "", 20, 17, 16, 20, { 6, 1, 2, -1, -1, -1, 45, -1, -1, -1, -1, -1 }, 90, 130, 6, 6, 5, 7360576u, 9465888u, 13393952u, 15523536u, 0 }, // 810 authentic-layered
    { "Balrog", "A massive black demon", "", 999, 250, 80, 200, { 124, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 450, 480, 11, 11, 30, 0u, 0u, 0u, 0u, 0 }, // 811 authentic-layered
    { "Silicius", "A Peaceful monk", "", 12, 13, 15, 12, { 6, 1, 2, -1, -1, -1, 76, 81, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 16761440u, 65535u, 255u, 15523536u, 0 }, // 812 authentic-layered
    { "Robin Banks", "A master thief", "", 34, 32, 37, 33, { 3, 4, 2, -1, -1, -1, -1, -1, 46, -1, -1, -1 }, 150, 230, 6, 6, 5, 1u, 2u, 3u, 15523536u, 0 }, // 813 needs-custom-sprite
    { "Mum", "The greatest woman in the world", "", 1, 99, 3, 1, { 3, 4, 2, -1, -1, -1, -1, -1, -1, -1, 9, -1 }, 145, 220, 6, 6, 5, 16752704u, 3211263u, 14540032u, 15523536u, 0 }, // 814 authentic-layered
    { "Ester", "She looks quite frazzled", "", 1, 99, 3, 1, { 3, 4, 2, -1, 122, 77, 76, 81, -1, -1, -1, 62 }, 145, 220, 6, 6, 5, 16763992u, 3211263u, 14540032u, 15523536u, 0 }, // 815 authentic-layered
    { "Bunny", "A fluffy bunny", "", 1, 1, 10, 1, { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 95, 85, 7, 7, 10, 0u, 0u, 0u, 0u, 1 }, // 816 needs-custom-sprite
    { "Duck", "Definitely not the ugly one", "", 1, 1, 10, 1, { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 85, 95, 6, 6, 5, 1u, 2u, 3u, 4u, 1 }, // 817 needs-custom-sprite
    { "PKBOT", "He looks scary.", "", 41, 99, 87, 1, { 0, 1, 2, -1, 47, 8, 76, 81, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 16761440u, 8409120u, 33415270u, 15523536u, 0 }, // 818 authentic-layered
    { "Death", "He sure could do with gaining some weight", "", 15, 15, 12, 12, { 3, 1, 2, -1, -1, -1, 76, 81, 46, -1, -1, -1 }, 145, 220, 6, 6, 5, 1u, 2u, 3u, 16777215u, 0 }, // 819 needs-custom-sprite
    { "Loan Officer", "He can lend me some money", "", 11, 8, 7, 11, { 0, 1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 15921906u, 2u, 3u, 7296823u, 0 }, // 820 authentic-layered
    { "Santa", "He sure could do with gaining some weight", "", 123, 123, 123, 123, { 6, 1, 2, -1, -1, 208, -1, -1, 46, -1, -1, -1 }, 160, 220, 6, 6, 5, 16777215u, 0u, 0u, 15523536u, 0 }, // 821 needs-custom-sprite
    { "Kresh", "He's kind of like an onion", "", 123, 123, 123, 123, { 7, 1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 160, 220, 6, 6, 5, 0u, 0u, 0u, 0u, 0 }, // 822 needs-custom-sprite
    { "Lily", "She has a green thumb", "", 1, 1, 10, 1, { 3, 4, 2, -1, -1, -1, -1, -1, 46, -1, 9, -1 }, 145, 220, 6, 6, 5, 0u, 0u, 0u, 15523536u, 0 }, // 823 needs-custom-sprite
    { "Peter Skippin", "Shut up, Meg", "", 20, 20, 20, 20, { 5, 1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 11167296u, 0u, 0u, 15523536u, 0 }, // 824 authentic-layered
    { "Mortimer", "A not-so-wealthy tradesman", "", 11, 8, 7, 11, { 0, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 15921906u, 2u, 0u, 15523536u, 0 }, // 825 needs-custom-sprite
    { "Randolph", "A not-so-wealthy tradesman", "", 11, 8, 7, 11, { 0, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 15921906u, 2u, 0u, 15523536u, 0 }, // 826 needs-custom-sprite
    { "Ana (not in a barrel)", "I should update my client.", "", 17, 15, 16, 18, { 3, 4, 2, -1, -1, -1, 76, 81, -1, -1, -1, -1 }, 120, 220, 6, 6, 5, 16760880u, 8409120u, 8409120u, 10056486u, 0 }, // 827 authentic-layered
    { "Biggum Flodrot", "Biggum Flodrot, goblin hero", "", 99, 99, 99, 99, { -1, 139, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 219, 206, 9, 8, 5, 0u, 0u, 0u, 0u, 0 }, // 828 needs-custom-sprite
    { "Spookie", "A spooky, scary skeleton!", "", 0, 0, 10, 1, { 133, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 216, 234, 11, 11, 5, 0u, 0u, 0u, 0u, 0 }, // 829 authentic-layered
    { "Scarie", "A spooky, scary skeleton!", "", 0, 0, 10, 1, { 133, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 216, 234, 11, 11, 5, 0u, 0u, 0u, 0u, 0 }, // 830 authentic-layered
    { "Todd Sandyman", "Some of the children call him \\\"The White Ogre\\\"", "", 0, 0, 3, 0, { 0, 1, 2, -1, 108, -1, -1, -1, -1, -1, -1, -1 }, 145, 220, 6, 6, 5, 16753488u, 0u, 0u, 15523536u, 0 }, // 831 authentic-layered
    { "Praeteritum", "The ghost of Christmas past", "", 15, 15, 5, 15, { 137, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 201, 243, 9, 9, 5, 0u, 0u, 0u, 0u, 0 }, // 832 authentic-layered
    { "Praesens", "The ghost of Christmas present", "", 15, 15, 5, 15, { 137, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 201, 243, 9, 9, 5, 0u, 0u, 0u, 0u, 0 }, // 833 authentic-layered
    { "Futurum", "The ghost of Christmas future", "", 15, 15, 5, 15, { 137, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 201, 243, 9, 9, 5, 0u, 0u, 0u, 0u, 0 }, // 834 authentic-layered
    // woodcutting-guild copy of the authentic Forester (350): same look, different stats, not attackable
    { "Forester", "He looks after McGrubor's wood", "", 24, 22, 17, 23, { 6, 1, 2, -1, 107, -1, 45, -1, -1, 11, -1, -1 }, 145, 220, 6, 6, 5, 1u, 56576u, 43520u, 15523536u, 0 }, // 835 authentic-layered
    { "McGrubor", "Grumpy old McGruber", "", 20, 20, 20, 20, { 6, 1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 160, 220, 6, 6, 5, 0xAAAAAAu, 12277060u, 0x007900u, 15523536u, 0 }, // 836 authentic-layered
    { "Ash", "Groovy", "", 20, 20, 20, 20, { 0, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, 160, 220, 6, 6, 5, 0x50321Eu, 0x00137Fu, 0x794B1Eu, 15523536u, 0 }, // 837 needs-custom-sprite
};

// custom OBJECT defs for the runecraft rune islands, object ids 1189..1235

#define CUSTOM_OBJECT_BASE 1189
// ids 1189..1235 inclusive (runecraft)
#define CUSTOM_OBJECT_COUNT 47

// custom OBJECT defs for the OpenRSC custom map regions, object ids 1236..1295
#define CUSTOM_OBJECT2_BASE 1236
#define CUSTOM_OBJECT2_TOP 1295 // inclusive
#define CUSTOM_OBJECT2_COUNT (CUSTOM_OBJECT2_TOP - CUSTOM_OBJECT2_BASE + 1) // 60

static const struct { int id; const char *name; const char *description;
    const char *command1; const char *command2; const char *model;
    unsigned char type; unsigned char width; unsigned char height; } CUSTOM_OBJECTS2[CUSTOM_OBJECT2_COUNT] = {
    {1236, "pipe", "a dirty sewer pipe", "Enter", "Examine", "wallpipe", 1, 1, 1},
    {1237, "pipe", "a dirty sewer pipe", "Enter", "Examine", "wallpipe", 1, 1, 1},
    {1238, "Christmas Tree", "A very festive tree", "Collect", "Examine", "xmastree", 1, 1, 1},
    {1239, "Decorated Tree", "A tree that gathers people around", "WalkTo", "Examine", "ornamenttree", 1, 1, 1},
    {1240, "nothing", "", "", "", "", 0, 1, 1},
    {1241, "Tunnel entrance", "I wonder where this leads...", "enter", "Examine", "caveentrance2", 1, 3, 1},
    {1242, "Rowboat", "This looks usable", "Travel", "Examine", "rowboat", 1, 2, 2},
    {1243, "Lemon Tree", "\"A tree filled with many ripe lemons", "Harvest", "Examine", "lemontree", 1, 1, 1},
    {1244, "Lime Tree", "A tree filled with many ripe limes", "Harvest", "Examine", "limetree", 1, 1, 1},
    {1245, "Apple Tree", "A tree filled with many ripe apples", "Harvest", "Examine", "appletree", 1, 1, 1},
    {1246, "Orange Tree", "A tree filled with many ripe oranges", "Harvest", "Examine", "orangetree", 1, 1, 1},
    {1247, "Grapefruit Tree", "A tree filled with many ripe grapefruits", "Harvest", "Examine", "grapefruittree", 1, 1, 1},
    {1248, "Banana Palm", "A palm containing many ripe bananas", "Harvest", "Examine", "bananapalm", 1, 1, 1},
    {1249, "Coconut Palm", "A palm containing many ripe coconuts", "Harvest", "Examine", "coconutpalm", 1, 1, 1},
    {1250, "Papaya Palm", "A palm containing many ripe papayas", "Harvest", "Examine", "papayapalm", 1, 1, 1},
    {1251, "Pineapple Plant", "A plant with many nice ripe pineapples", "Harvest", "Examine", "pineappleplant", 1, 1, 1},
    {1252, "Exhausted Tree", "Someone has taken the last of the produce!", "WalkTo", "Examine", "exhaustedtree", 1, 1, 1},
    {1253, "Exhausted Palm", "Someone has taken the last of the produce!", "WalkTo", "Examine", "exhaustedpalm", 1, 1, 1},
    {1254, "Exhausted Palm", "Someone has taken the last of the produce!", "WalkTo", "Examine", "exhaustedpalm2", 1, 1, 1},
    {1255, "Exhausted Plant", "A plant that got its produce taken away", "WalkTo", "Examine", "depletedplant", 1, 1, 1},
    {1256, "Redberry Bush", "A bush containing some redberries", "Harvest", "Examine", "redberrybush", 1, 1, 1},
    {1257, "Cadavaberry Bush", "A bush containing some cadavaberries", "Harvest", "Examine", "cadavaberrybush", 1, 1, 1},
    {1258, "Dwellberry Bush", "A bush filled with mysterious dwellberries", "Harvest", "Examine", "dwellberrybush", 1, 1, 1},
    {1259, "Jangerberry Bush", "A bush having the mysterious jangerberries", "Harvest", "Examine", "jangerberrybush", 1, 1, 1},
    {1260, "Whiteberry Bush", "A bush containing some whiteberries", "Harvest", "Examine", "whiteberrybush", 1, 1, 1},
    {1261, "Depleted Bush", "A bush that once contained berries", "WalkTo", "Examine", "depletedbush", 1, 1, 1},
    {1262, "Cabbage", "Oooh some cabbage", "Harvest", "Examine", "greencabbage", 0, 1, 1},
    {1263, "Red Cabbage", "Oooh some red cabbage", "Harvest", "Examine", "redcabbage", 0, 1, 1},
    {1264, "White Pumpkin", "A pumpkin ready for harvest", "Harvest", "Examine", "pumpkinwhite", 0, 1, 1},
    {1265, "Potato Plant", "Some nice looking potatoes growing underneath", "Harvest", "Examine", "potatoplant", 0, 1, 1},
    {1266, "Onion Plant", "Some nice onions growing underneath", "Harvest", "Examine", "onionplant", 0, 1, 1},
    {1267, "Garlic Plant", "Some garlic growing underneath", "Harvest", "Examine", "garlicplant", 0, 1, 1},
    {1268, "Tomato Plant", "This plant has some good looking tomatoes", "Harvest", "Examine", "tomatoplant", 0, 1, 1},
    {1269, "Corn Plant", "This plant contains ripe corn", "Harvest", "Examine", "cornplant", 0, 1, 1},
    {1270, "Damaged Ground", "Disturbed ground left after a harvest", "WalkTo", "Examine", "dugupsoil1", 0, 1, 1},
    {1271, "Depleted tomato plant", "A plant that got its produce taken away", "WalkTo", "Examine", "depletedtomato", 0, 1, 1},
    {1272, "Depleted corn plant", "A plant that got its produce taken away", "WalkTo", "Examine", "depletedcorn", 0, 1, 1},
    {1273, "Snape Grass", "Some interesting snape grass growing here", "Clip", "Examine", "snapegrass", 1, 1, 1},
    {1274, "Herb", "I wonder what herb is around", "Clip", "Examine", "herb", 1, 1, 1},
    {1275, "Pumpkin", "A pumpkin of autumn", "Harvest", "Examine", "pumpkinwhite", 0, 1, 1},
    {1276, "Soil Mound", "A pile of very good soil", "WalkTo", "Examine", "soilmound", 1, 1, 1},
    {1277, "Barrel of water", "A barrel filled with filtered water", "WalkTo", "Examine", "barrelwater", 1, 1, 1},
    {1278, "nothing", "", "", "", "", 0, 1, 1},
    {1279, "nothing", "", "", "", "", 0, 1, 1},
    {1280, "Sea Weed", "Some tall sea weed growing here", "Clip", "Examine", "seaweed", 1, 1, 1},
    {1281, "Limpwurt Root", "Some nice limpwurt root around here", "Clip", "Examine", "limpwurtroot", 1, 1, 1},
    {1282, "Sugar Cane", "The plant of interesting sugar cane!", "Harvest", "Examine", "sugarcane", 0, 1, 1},
    {1283, "Mysterious Grape Vine", "This vine may have more than just grapes", "Harvest", "Examine", "grapevine", 0, 1, 1},
    {1284, "Lava Forge", "The latest of dwarven technology", "WalkTo", "Examine", "furnace", 1, 2, 2},
    {1285, "anvil", "heavy metal", "WalkTo", "Examine", "anvil", 1, 1, 1},
    {1286, "Rocks", "This looks dangerous...", "climb", "Examine", "brownclimbingrocks", 0, 1, 1},
    {1287, "Stepping Stone", "It looks like I could jump on this", "jump to", "Examine", "stonedisc", 1, 1, 1},
    {1288, "Stepping Stone", "It looks like I could jump on this", "jump to", "Examine", "stonedisc", 1, 1, 1},
    {1289, "Stepping Stone", "It looks like I could jump on this", "WalkTo", "Examine", "stonedisc", 1, 1, 1},
    {1290, "Handholds", "I wonder if I can climb up these", "climb", "Examine", "climbing_rocks", 0, 1, 1},
    {1291, "Stepping Stone", "It looks like I could jump on this", "jump to", "Examine", "stonedisc", 1, 1, 1},
    {1292, "Stepping Stone", "It looks like I could jump on this", "jump to", "Examine", "stonedisc", 1, 1, 1},
    {1293, "Dragonfruit Tree", "A tree filled with many ripe dragonfruits", "Harvest", "Examine", "dragonfruit", 1, 1, 1},
    {1294, "Exhausted Tree", "Someone has taken the last of the produce!", "WalkTo", "Examine", "depleteddragonfruit", 1, 1, 1},
    {1295, "Stepping Stone", "It looks like I could jump on this", "jump to", "Examine", "stonedisc", 1, 1, 1},
};

// rune names indexed by (altar_id - 1191) / 2
static const char *RUNE_ALTAR_NAMES[12] = {
    "Air",  "Mind",  "Water", "Earth", "Fire",  "Body",
    "Cosmic", "Chaos", "Nature", "Law",  "Death", "Blood"};

// register a model name by NAME, freeing strdup if the slot already exists
static int register_model(const char *name) {
    char *dup = strdup(name);
    int old_count = game_data.model_count;
    int index = game_data_get_model_index(dup);
    if (game_data.model_count == old_count) {
        free(dup);
    }
    return index;
}

void game_data_append_custom_objects(void) {
    // only append when the base table is the expected 1189-long authentic table
    if (game_data.object_count > CUSTOM_OBJECT_BASE) {
        return;
    }

    // runecraft ids 1189..1235, then custom-map ids 1236..1295 -> table len 1296
    int obj_total = CUSTOM_OBJECT2_BASE + CUSTOM_OBJECT2_COUNT; // == 1296

    struct ObjectConfig *objects =
        realloc(game_data.objects, obj_total * sizeof(struct ObjectConfig));

    if (objects == NULL) {
        return;
    }

    game_data.objects = objects;

    // fill any gap if the base table is somehow shorter than 1189
    for (int i = game_data.object_count; i < CUSTOM_OBJECT_BASE; i++) {
        memset(&objects[i], 0, sizeof(struct ObjectConfig));
        objects[i].name = strdup("nothing");
        objects[i].description = strdup("");
        objects[i].command1 = strdup("");
        objects[i].command2 = strdup("");
    }

    // resolve model indices by name, reusing existing slots if present
    int altar_model = register_model("altar");
    int ruins_model = register_model("mysterious ruins");
    int portal_model = register_model("portal");
    int mine_model = register_model("essencemine");

    for (int id = CUSTOM_OBJECT_BASE; id < CUSTOM_OBJECT2_BASE; id++) {
        struct ObjectConfig *oc = &objects[id];
        memset(oc, 0, sizeof(struct ObjectConfig));
        oc->elevation = 0;

        if (id == CUSTOM_OBJECT_BASE) {
            // 1189: filler so 1190+ land at exact ids
            oc->name = strdup("nothing");
            oc->description = strdup("");
            oc->command1 = strdup("");
            oc->command2 = strdup("");
            oc->model_index = altar_model;
            oc->width = 1;
            oc->height = 1;
            oc->type = 0;
        } else if (id >= 1190 && id <= 1213) {
            int is_bind = (id % 2) == 1; // ODD = bind altar, EVEN = ruins

            if (is_bind) {
                const char *rune = RUNE_ALTAR_NAMES[(id - 1191) / 2];
                char name_buf[32];
                snprintf(name_buf, sizeof(name_buf), "%s Altar", rune);
                oc->name = strdup(name_buf);
                oc->description =
                    strdup("A mysterious power eminates from this shrine");
                oc->command1 = strdup("Bind");
                oc->command2 = strdup("Examine");
                oc->model_index = altar_model;
                oc->width = 2;
                oc->height = 2;
                oc->type = 1; // blocked
            } else {
                oc->name = strdup("Mysterious Ruins");
                oc->description =
                    strdup("A mysterious power eminates from this shrine");
                oc->command1 = strdup("Enter");
                oc->command2 = strdup("Examine");
                oc->model_index = ruins_model;
                oc->width = 3;
                oc->height = 3;
                oc->type = 1; // blocked
            }
        } else if (id == 1227) {
            // rune-essence mine
            oc->name = strdup("Raw Rune stone");
            oc->description = strdup("A pile of raw rune stone");
            oc->command1 = strdup("Mine");
            oc->command2 = strdup("Examine");
            oc->model_index = mine_model;
            oc->width = 6;
            oc->height = 6;
            oc->type = 0; // unblocked per XML
        } else {
            // 1214..1226, 1228..1235: the island portals (Exit/Take).
            int big = (id >= 1216 && id <= 1226); // 2x2 per XML; rest 1x1
            oc->name = strdup("Portal");
            oc->description = strdup("This will lead you out");
            oc->command1 = strdup(id >= 1228 ? "Take" : "Exit");
            oc->command2 = strdup("Examine");
            oc->model_index = portal_model;
            oc->width = big ? 2 : 1;
            oc->height = big ? 2 : 1;
            oc->type = 0; // unblocked per XML
        }
    }

    // custom-map object defs (ids 1236..1295)
    for (int i = 0; i < CUSTOM_OBJECT2_COUNT; i++) {
        int id = CUSTOM_OBJECTS2[i].id;
        struct ObjectConfig *oc = &objects[id];
        memset(oc, 0, sizeof(struct ObjectConfig));
        oc->elevation = 0;
        oc->name = strdup(CUSTOM_OBJECTS2[i].name);
        oc->description = strdup(CUSTOM_OBJECTS2[i].description);
        oc->command1 = strdup(CUSTOM_OBJECTS2[i].command1);
        oc->command2 = strdup(CUSTOM_OBJECTS2[i].command2);
        // empty model name is a filler slot; leave model_index at 0
        oc->model_index = CUSTOM_OBJECTS2[i].model[0]
            ? register_model(CUSTOM_OBJECTS2[i].model) : 0;
        oc->width = CUSTOM_OBJECTS2[i].width;
        oc->height = CUSTOM_OBJECTS2[i].height;
        oc->type = CUSTOM_OBJECTS2[i].type;
    }

    game_data.object_count = obj_total;
}

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
// base animation index where custom-entity animations were appended; -1 until appended
static int custom_entity_anim_base = -1;

// append the 59 OpenRSC custom-entity animations to game_data.animations[]
static void game_data_append_custom_entity_animations(void) {
    if (custom_entity_anim_base != -1) {
        return; // already appended
    }

    // skip only when a config already defines every custom name. the
    // authentic table's last entry is also named "scythe", so an any-name
    // check aborted the whole append and hid every custom layer
    int present = 0;
    for (int k = 0; k < GL_CUSTOM_ENTITY_ANIM_COUNT; k++) {
        for (int i = 0; i < game_data.animation_count; i++) {
            const char *n = game_data.animations[i].name;
            if (n != NULL && strcmp(n, gl_custom_entity_anims[k].name) == 0) {
                present++;
                break;
            }
        }
    }
    if (present == GL_CUSTOM_ENTITY_ANIM_COUNT) {
        return; // the config ships them all, do not duplicate
    }

    int base = game_data.animation_count;
    int total = base + GL_CUSTOM_ENTITY_ANIM_COUNT;

    struct AnimConfig *anims =
        realloc(game_data.animations, total * sizeof(struct AnimConfig));
    if (anims == NULL) {
        return;
    }
    game_data.animations = anims;

    for (int k = 0; k < GL_CUSTOM_ENTITY_ANIM_COUNT; k++) {
        struct AnimConfig *ac = &anims[base + k];
        memset(ac, 0, sizeof(struct AnimConfig));
        ac->name = strdup(gl_custom_entity_anims[k].name);
        ac->colour = gl_custom_entity_anims[k].colour;
        ac->gender = 0; // never enters the appearance-design head/body cycle
        ac->has_a = 1; // 15 walk + 3 combat (EQUIP_COMBAT layout)
        ac->has_f = 0;
        ac->file_id = gl_custom_entity_anims[k].file_id;
    }

    game_data.animation_count = total;
    custom_entity_anim_base = base;

    // log confirmation next to the sheet-load line
    mud_error("[gfx] custom entity anims appended at %d\n", base);
}

// resolve a custom-entity animation NAME to its animations[] index, or -1
static int custom_entity_anim_index(const char *name) {
    if (custom_entity_anim_base < 0) {
        return -1;
    }
    for (int k = 0; k < GL_CUSTOM_ENTITY_ANIM_COUNT; k++) {
        if (strcmp(gl_custom_entity_anims[k].name, name) == 0) {
            return custom_entity_anim_base + k;
        }
    }
    return -1;
}

// map each of the 4 no-body custom NPCs to its custom-entity body animation name
static const struct {
    int npc_id;
    const char *anim_name;
} CUSTOM_NOBODY_NPCS[] = {
    {800, "kiteshield"}, // Gaia
    {804, "fishingcape"}, // Greatwood
    {816, "bunny"}, // Bunny
    {817, "duck"}, // Duck
};
#define CUSTOM_NOBODY_NPC_COUNT \
    ((int)(sizeof(CUSTOM_NOBODY_NPCS) / sizeof(CUSTOM_NOBODY_NPCS[0])))

// map a single extra custom-entity equipment layer onto an otherwise authentic-layered NPC
static const struct {
    int npc_id;
    int slot;
    const char *anim_name;
} CUSTOM_LAYERED_ANIM_OVERRIDES[] = {
    {802, 7, "armorskirt"}, // Ultimate Ironman tutor, legs
    {813, 6, "fleatherbody"}, // Robin Banks, torso
    {819, 4, "scythe"}, // Death, weapon
    {819, 5, "deathmask"}, // Death, mask
    {821, 6, "santabody"}, // Santa, torso
    {821, 7, "santalegs"}, // Santa, legs
    {822, 5, "ogreears"}, // Kresh, ears
    {822, 6, "leathervest"}, // Kresh, vest
    {823, 11, "harvestingcape"}, // Lily, cape
    {825, 6, "mortimertorso"}, // Mortimer, torso
    {826, 6, "randolphtorso"}, // Randolph, torso
    {828, 0, "biggum"}, // Biggum Flodrot, hero overlay
    {837, 1, "ashtorso"}, // Ash, torso
};
#define CUSTOM_LAYERED_ANIM_OVERRIDE_COUNT \
    ((int)(sizeof(CUSTOM_LAYERED_ANIM_OVERRIDES) / sizeof(CUSTOM_LAYERED_ANIM_OVERRIDES[0])))
#endif // RENDER_GL || RENDER_3DS_GL

void game_data_append_custom(void) {
    int total = CUSTOM_ITEM_BASE + CUSTOM_ITEM_COUNT;
    struct ItemConfig *items = realloc(game_data.items, total * sizeof(struct ItemConfig));
    if (items == NULL) { return; }
    game_data.items = items;

    // fill any gap if the client base item table is shorter than expected
    for (int i = game_data.item_count; i < CUSTOM_ITEM_BASE; i++) {
        memset(&items[i], 0, sizeof(struct ItemConfig));
        items[i].name = strdup("nothing");
        items[i].description = strdup("");
        items[i].command = strdup("");
    }

    for (int i = 0; i < CUSTOM_ITEM_COUNT; i++) {
        struct ItemConfig *it = &items[CUSTOM_ITEM_BASE + i];
        memset(it, 0, sizeof(struct ItemConfig));
        it->name = strdup(CUSTOM_ITEMS[i].name);
        it->description = strdup(CUSTOM_ITEMS[i].description);
        it->command = strdup(CUSTOM_ITEMS[i].command);
        it->sprite = CUSTOM_ITEMS[i].sprite;
        it->base_price = CUSTOM_ITEMS[i].price;
        it->wearable = CUSTOM_ITEM_WEARABLE[i]; // equip-slot bitmask (0 if none)
        it->mask = 0;
        // stackable polarity: 0 = stacks/carries an amount on the wire
        it->stackable = CUSTOM_ITEMS[i].stackable ? 0 : 1;
        it->special = CUSTOM_ITEMS[i].special;
        it->members = CUSTOM_ITEMS[i].members;
        // deliberately not bumping item_sprite_count: custom icons live in a separate GL atlas
    }
    game_data.item_count = total;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    // append the custom-entity animations before wiring the NPCs
    game_data_append_custom_entity_animations();
#endif

    // custom NPCs: feed each NPC's real recovered appearance instead of cloning the base Man
    int npc_total = CUSTOM_NPC_BASE + CUSTOM_NPC_COUNT;
    // guard before realloc: leave the table untouched if a full config already defines these ids
    struct NpcConfig *npcs = game_data.npc_count <= CUSTOM_NPC_BASE
        ? realloc(game_data.npcs, npc_total * sizeof(struct NpcConfig))
        : NULL;
    if (npcs != NULL) {
        game_data.npcs = npcs;
        struct NpcConfig clone = npcs[CUSTOM_NPC_CLONE_ID];
        for (int i = game_data.npc_count; i < CUSTOM_NPC_BASE; i++) {
            memset(&npcs[i], 0, sizeof(struct NpcConfig));
            npcs[i].name = strdup("nothing");
            npcs[i].description = strdup("");
            npcs[i].command = strdup("");
        }
        for (int i = 0; i < CUSTOM_NPC_COUNT; i++) {
            struct NpcConfig *nc = &npcs[CUSTOM_NPC_BASE + i];
            int npc_id = CUSTOM_NPC_BASE + i;

            // under GL, resolve a no-body NPC's custom-entity body animation index; -1 elsewhere
            int nobody_anim = -1;
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
            if (CUSTOM_NPCS[i].use_clone) {
                for (int n = 0; n < CUSTOM_NOBODY_NPC_COUNT; n++) {
                    if (CUSTOM_NOBODY_NPCS[n].npc_id == npc_id) {
                        nobody_anim =
                            custom_entity_anim_index(CUSTOM_NOBODY_NPCS[n].anim_name);
                        break;
                    }
                }
            }
#endif

            if (CUSTOM_NPCS[i].use_clone && nobody_anim < 0) {
                // custom-entity atlas absent or animation missing: keep the Man appearance as a placeholder
                *nc = clone;
            } else {
                memset(nc, 0, sizeof(struct NpcConfig));
                for (int j = 0; j < NPC_SPRITE_COUNT; j++) {
                    nc->sprites[j] = CUSTOM_NPCS[i].sprites[j];
                }
                // no-body NPC: whole body is one custom-entity sprite at layer 0
                if (nobody_anim >= 0) {
                    nc->sprites[0] = (int16_t)nobody_anim;
                }
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
                // layered custom-entity equipment; an NPC can carry several
                // rows, so apply every match (a slot only changes when its
                // animation resolves)
                for (int o = 0; o < CUSTOM_LAYERED_ANIM_OVERRIDE_COUNT; o++) {
                    if (CUSTOM_LAYERED_ANIM_OVERRIDES[o].npc_id != npc_id) {
                        continue;
                    }
                    int idx = custom_entity_anim_index(
                        CUSTOM_LAYERED_ANIM_OVERRIDES[o].anim_name);
                    if (idx >= 0) {
                        nc->sprites[CUSTOM_LAYERED_ANIM_OVERRIDES[o].slot] =
                            (int16_t)idx;
                    }
                }
#endif
                nc->width = CUSTOM_NPCS[i].width;
                nc->height = CUSTOM_NPCS[i].height;
                nc->walk_speed = CUSTOM_NPCS[i].walk_speed;
                nc->combat_speed = CUSTOM_NPCS[i].combat_speed;
                nc->combat_width = CUSTOM_NPCS[i].combat_width;
                nc->attackable = clone.attackable;
                nc->hair_colour = (int)CUSTOM_NPCS[i].hair_colour;
                nc->top_colour = (int)CUSTOM_NPCS[i].top_colour;
                nc->bottom_colour = (int)CUSTOM_NPCS[i].bottom_colour;
                nc->skin_colour = (int)CUSTOM_NPCS[i].skin_colour;
            }

            nc->name = strdup(CUSTOM_NPCS[i].name);
            nc->description = strdup(CUSTOM_NPCS[i].description);
            nc->command = strdup(CUSTOM_NPCS[i].command);
            nc->attack = CUSTOM_NPCS[i].attack;
            nc->strength = CUSTOM_NPCS[i].strength;
            nc->hits = CUSTOM_NPCS[i].hits;
            nc->defense = CUSTOM_NPCS[i].defense;
        }
        game_data.npc_count = npc_total;
    }

    // append the runecraft altar + rune-stone object defs (ids 1189..1227)
    game_data_append_custom_objects();

    // give bankers a Bank right-click command to open the bank directly
    {
        static const int BANKER_IDS[] = {95, 224, 268, 540, 617, 792};
        for (size_t b = 0; b < sizeof(BANKER_IDS) / sizeof(BANKER_IDS[0]); b++) {
            int id = BANKER_IDS[b];
            if (id >= 0 && id < game_data.npc_count) {
                game_data.npcs[id].command = strdup("Bank");
            }
        }
    }
}

void surface_setup_custom_item_sprites(Surface *surface, int sprite_item_base) {
    for (int i = 0; i < CUSTOM_SPRITE_COUNT; i++) {
        int sid = sprite_item_base + CUSTOM_SPRITES[i].slot;
        surface->sprite_width[sid] = CUSTOM_SPRITES[i].w;
        surface->sprite_height[sid] = CUSTOM_SPRITES[i].h;
        surface->sprite_width_full[sid] = CUSTOM_SPRITES[i].w;
        surface->sprite_height_full[sid] = CUSTOM_SPRITES[i].h;
        surface->sprite_translate_x[sid] = 0;
        surface->sprite_translate_y[sid] = 0;
        surface->sprite_translate[sid] = 0;
    }
}

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
// populate surface geometry arrays for the custom-entity sprite id range
void surface_setup_custom_entity_sprites(Surface *surface) {
    for (int k = 0; k < GL_CUSTOM_ENTITY_SLOT_COUNT; k++) {
        int sid = GL_CUSTOM_ENTITY_FILE_BASE + k;
        const gl_custom_entity_geom *g = &gl_custom_entity_geoms[k];

        int full_w = g->full_w ? g->full_w : (g->w ? g->w : 1);
        int full_h = g->full_h ? g->full_h : (g->h ? g->h : 1);

        surface->sprite_width[sid] = g->w;
        surface->sprite_height[sid] = g->h;
        surface->sprite_width_full[sid] = (int16_t)full_w;
        surface->sprite_height_full[sid] = (int16_t)full_h;
        surface->sprite_translate_x[sid] = g->offset_x;
        surface->sprite_translate_y[sid] = g->offset_y;
        surface->sprite_translate[sid] = 1;
    }
}
#endif
