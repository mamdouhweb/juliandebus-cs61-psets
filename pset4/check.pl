#! /usr/bin/perl
use Time::HiRes qw(gettimeofday);
my($nkilled) = 0;
my($nerror) = 0;
my(@ratios);
my(@times);

sub makefile ($$) {
    my($filename, $size) = @_;
    if (!-r $filename || -s $filename != $size) {
	truncate($filename, 0);
	while (-s $filename < $size) {
	    system("cat /usr/share/dict/words >> $filename");
	}
	truncate($filename, $size);
    }
}

sub run_time ($;$) {
    my($command, $max_time) = @_;
    $max_time = 30 if !$max_time;
    my($pid) = fork();
    if ($pid == 0) {
	sleep 0.05;
	exec($command);
    }
    my($before) = Time::HiRes::time() + 0.05;
    my($died) = 0;
    eval {
	local $SIG{"CHLD"} = sub { die "!" };
	sleep $max_time;
	$died = 1;
	kill 9, $pid;
    };
    return undef if $died;
    my($delta) = Time::HiRes::time() - $before;
    return $delta <= 0 ? 0.000001 : $delta;
}

sub run ($$$$;$) {
    my($number, $infile, $command, $desc, $max_time) = @_;
    return if (@ARGV && !grep {
	$_ == $number
	    || ($_ =~ m{^(\d+)-(\d+)$} && $number >= $1 && $number <= $2)
	    || ($_ =~ m{(?:^|,)$number(,|$)})
	       } @ARGV);
    $max_time = 30 if !$max_time;
    my($base) = $command;
    my($crap) = `md5sum $infile`;
    $base =~ s<(\./)([a-z]*61)><${1}stdio-$2>g;
    $base =~ s<out\.txt><baseout\.txt>g;
    print "TEST:      $number. $desc\nCOMMAND:   $command\nSTDIO:     ";
    my($t) = run_time($base);
    printf "%.3fs\nYOUR CODE: ", $t;
    $crap = `md5sum $infile`;
    my($tt) = run_time($command);
    if (!defined($tt)) {
	printf "KILLED after %gs\n", $max_time;
	++$nkilled;
    } else {
	printf "%.3fs (%.2fx stdio)\n", $tt, ($t / $tt);
	push @ratios, $t / $tt;
	push @times, $t;
    }
    if ($base =~ m<files/baseout\.txt>
	&& `cmp files/baseout.txt files/out.txt >/dev/null 2>&1 || echo OOPS` eq "OOPS\n") {
	print "           ERROR! files/out.txt differs from base version\n";
	++$nerror;
    }
    print "\n";
}

sub pl ($$) {
    my($n, $x) = @_;
    return $n . " " . ($n == 1 ? $x : $x . "s");
}

sub summary () {
    my($ntests) = @ratios + $nkilled;
    print "SUMMARY:   ", pl($ntests, "test"),
	", $nkilled killed, ", pl($nerror, "error"), "\n";
    my($better) = scalar(grep { $_ > 1 } @ratios);
    my($worse) = scalar(grep { $_ < 1 } @ratios);
    print "           better than stdio ", pl($better, "time"),
    ", worse ", pl($worse, "time"), "\n";
    my($mean, $time, $yourtime) = (0, 0, 0);
    for (my $i = 0; $i < @ratios; ++$i) {
	$mean += $ratios[$i];
	$time += $times[$i];
	$yourtime += $times[$i] / $ratios[$i];
    }
    printf "           average %.2fx stdio\n", $mean / @ratios;
    printf "           total time %.3fs stdio, %.3fs your code (%.2fx stdio)\n",
    $time, $yourtime, $time / $yourtime;
}

# create some files
mkdir "files" if !-d "files";
makefile("files/text1meg.txt", 1 << 20);
makefile("files/text5meg.txt", 5 << 20);
makefile("files/text20meg.txt", 20 << 20);

$SIG{"INT"} = sub {
    summary();
    exit(1);
};

run(1, "files/text1meg.txt",
    "./cat61 files/text1meg.txt > files/out.txt",
    "sequential regular small file 1B", 10);

run(2, "files/text1meg.txt",
    "cat files/text1meg.txt | ./cat61 | cat > files/out.txt",
    "sequential piped small file 1B", 10);

run(3, "files/text5meg.txt",
    "./cat61 files/text5meg.txt > files/out.txt",
    "sequential regular medium file 1B", 10);

run(4, "files/text5meg.txt",
    "cat files/text5meg.txt | ./cat61 | cat > files/out.txt",
    "sequential piped medium file 1B", 10);

run(5, "files/text20meg.txt",
    "./cat61 files/text20meg.txt > files/out.txt",
    "sequential regular large file 1B", 20);

run(6, "files/text20meg.txt",
    "cat files/text20meg.txt | ./cat61 | cat > files/out.txt",
    "sequential piped large file 1B", 20);

run(7, "files/text5meg.txt",
    "./blockcat61 -b 1024 files/text5meg.txt > files/out.txt",
    "sequential regular medium file 1KB", 10);

run(8, "files/text5meg.txt",
    "cat files/text5meg.txt | ./blockcat61 -b 1024 | cat > files/out.txt",
    "sequential piped medium file 1KB", 10);

run(9, "files/text20meg.txt",
    "./blockcat61 -b 1024 files/text20meg.txt > files/out.txt",
    "sequential regular large file 1KB", 20);

run(10, "files/text20meg.txt",
    "cat files/text20meg.txt | ./blockcat61 -b 1024 | cat > files/out.txt",
    "sequential piped large file 1KB", 20);

run(11, "files/text20meg.txt",
    "./blockcat61 files/text20meg.txt > files/out.txt",
    "sequential regular large file 4KB", 20);

run(12, "files/text20meg.txt",
    "cat files/text20meg.txt | ./blockcat61 | cat > files/out.txt",
    "sequential piped large file 4KB", 20);

run(13, "files/text20meg.txt",
    "./randomcat61 -b 1024 files/text20meg.txt > files/out.txt",
    "sequential regular large file random blocks 1KB", 10);

run(14, "files/text20meg.txt",
    "./randomcat61 -b 1024 -s 6582 files/text20meg.txt > files/out.txt",
    "sequential regular large file random blocks 1KB", 10);

run(15, "files/text20meg.txt",
    "./randomcat61 files/text20meg.txt > files/out.txt",
    "sequential regular large file random blocks 4KB", 10);

run(16, "files/text20meg.txt",
    "./randomcat61 -s 6582 files/text20meg.txt > files/out.txt",
    "sequential regular large file random blocks 4KB", 10);

run(17, "files/text20meg.txt",
    "./reordercat61 files/text20meg.txt > files/out.txt",
    "reordered regular large file", 20);

run(18, "files/text20meg.txt",
    "./reordercat61 -s 6582 files/text20meg.txt > files/out.txt",
    "reordered regular large file", 20);

run(19, "files/text5meg.txt",
    "./reverse61 files/text5meg.txt > files/out.txt",
    "reversed medium file", 20);

run(20, "files/text5meg.txt",
    "./stridecat61 -s 1048576 files/text5meg.txt > files/out.txt",
    "1MB stride medium file", 20);

summary();
