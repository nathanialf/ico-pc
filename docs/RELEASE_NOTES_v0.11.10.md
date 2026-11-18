## Fixed

- An attempted fix for the game closing with "The graphics driver stopped responding" a few seconds into the first 3D scene on some Adreno phones (Galaxy A36). A background check of the character models now never prepares its own extra graphics work on phones, which matches the one start on that phone that kept running. Whether it helps there is not known yet: the log of the next start will tell.
