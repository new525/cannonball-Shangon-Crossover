# Player 1 (monitor A) into a race with MAD POWER, automatic, from a fresh EEPROM:
# coin, START, steering left twice at the car select (EASY HANDLING -> SMOOTH... -> MAD POWER),
# the transmission left at its default (automatic) until the selection times out.
# Race substates ($208082): 3 car select from frame 767, 5 setup at 2507, the course is loaded at 2571;
# the player car's state $20040C = 2 (driving) from 2869. Full accelerator from 2900.
600 set SERVICE12_A 04 1
606 set SERVICE12_A 04 0
700 set SERVICE12_A 10 1
760 set SERVICE12_A 10 0
900 aset ANALOG1 ff 0
920 aset ANALOG1 ff 128
1050 aset ANALOG1 ff 0
1070 aset ANALOG1 ff 128
2900 aset ANALOG2 ff 255
