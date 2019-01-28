#!/bin/bash

if (($# != 4)); then
	echo "Give: root_dir text_file w p"; exit
else
	rootDir=$1; textFile=$2; w=$3; p=$4;
fi

if [ ! -f $textFile ]; then #elegxos arxeiou
	echo "Text file doesn't exist."; exit
fi

if [ ! -d $rootDir ]; then #elegxos directory
	echo "Root Dir doesn't exist."; exit
fi

lines=`wc -l < $textFile` #elegxos grammwn
if (($lines <= 10000)); then
	echo "File is too short"; exit
fi

declare -A allpages #2D array with names

contentsOfDir=`ls $rootDir`  #elegxos adeiou directory
if ((`expr length "$contentsOfDir"` > 0)); then
	echo "Warning: directory is full, purging"; rm -rf $rootDir/*;
fi

#Gemisma tou pinaka me ta onomata selidwn
for ((i=0; i<$w; i++)); do
	for ((j=0; j<$p; j++)); do
		suffix=$((RANDOM))
		pagename="/site$i/page$i_$suffix.html"
		echo $pagename >> allpages
		allpages[$i,$j]=$pagename
	done
done

for ((i=0; i<$w; i++)); do  #gia kathe site
	dirName="$rootDir/site$i"; #neos katalogos
	echo "Creating web site $dirName" ; mkdir $dirName
	
	for ((j=0; j<$p; j++)); do #gia kathe page
		maxModulo=$(($lines - 2000)) ; k=$((RANDOM % $maxModulo)); ((k++))
		m=$((RANDOM % 1000)); ((m+=1000))
		f=$((p/2)); ((f++))
		q=$((w/2)); ((q++))

		pagename="$rootDir${allpages[$i,$j]}"
		echo "..Creating page $pagename with $m lines starting at line $k..."
		echo "<!DOCTYPE html><html><body>" > $pagename #headers
		
		q_count=0; f_count=0;
		for ((n=0; ; n++)); do
		
			read line
			if ((n < $k)); then
				continue;
			elif ((n == $k)); then
				mi=0
				echo $line >> $pagename
			else
				echo $line >> $pagename
			fi
			
			if (($mi != 0 && $mi % $((m/(f+q))) == 0)); then
				if (($q_count < $q)); then #print external link
					for ((x=$i; $x == $i; )); do x=$((RANDOM % $w)) ; done
					echo "....Adding link to $rootDir${allpages[$x,$q_count]}"
					echo "<a href=\"..${allpages[$x,$q_count]}\">link $q_count external</a>" >> $pagename
					echo "${allpages[$x,$q_count]}" >> mypages
					((q_count++))
				elif (($f_count < $f)); then #print internal link
					echo "....Adding link to $rootDir${allpages[$i,$f_count]}"
					echo "<a href=\"..${allpages[$i,$f_count]}\">link $f_count internal</a>" >> $pagename
					echo "${allpages[$i,$f_count]}" >> mypages
					((f_count++))	
				fi
			fi
			
			if (($q_count == $q && $f_count == $f)); then
				break;
			fi
			((mi++))
		done < $textFile		
		echo "</body></html>" >> $pagename #footers
	done
done

sort mypages | uniq > mypagesSorted
sort allpages > allpagesSorted
difference=`diff mypagesSorted allpagesSorted`
if ((`expr length "$difference"` > 0)); then
	echo "All pages do not have at least one incoming link"
else
	echo "All pages have at least one incoming link"
fi
rm -f allpages mypages allpagesSorted mypagesSorted
echo "Done"
