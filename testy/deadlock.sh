
runProgram() {
	NE=$(($RANDOM%1000+1))
	NR=$(($RANDOM%20+1))
	TimeWorkingElf=$(($RANDOM%1001))
	TimeReturningHome=$(($RANDOM%1001))
	
	echo | ../proj2 $NE $NR $TimeWorkingElf $TimeReturningHome
	
}

for i in $(seq 1 10);
do
	runProgram;
done

echo "Deadlock nenastal"
