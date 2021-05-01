
runProgram() {
	NE=$(($RANDOM%1000+1))
	NR=$(($RANDOM%20+1))
	TimeWorkingElf=$(($RANDOM%1001))
	TimeReturningHome=$(($RANDOM%1001))
	
	"$program" $NE $NR $TimeWorkingElf $TimeReturningHome
	python ./python_test.py > python_test_output.txt
	cat python_test_output.txt | mytest
}

mytest() {
	if [ "$1" != "" ]; then
		echo "Nastala chyba: $1";
	fi
}
program="../proj2" 

if [ "$1" != "" ]; then
	program="$1"
fi

gcc $program.c -std=gnu99 -Wall -Wextra -pedantic -pthread -o $program

for i in $(seq 1 10000);
do
	echo "Začínám provádět další program $program"
	runProgram "$program";
done

