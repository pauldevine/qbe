#!/usr/bin/perl
# G8 workaround: split file-scope `T a = 1, b = 2;` (first declarator
# initialized) into `T a = 1; T b = 2;`.  Only at brace depth 0.
local $/; my $s = <STDIN>; my $out = ''; my $depth = 0; my $stmt = '';
my ($i, $n) = (0, length $s);
my $instr = 0; my $q = '';
for ($i = 0; $i < $n; $i++) {
  my $c = substr($s, $i, 1);
  if ($depth == 0) { $stmt .= $c } else { $out .= $c }
  if ($instr) { if ($c eq '\\') { $i++; my $d = substr($s,$i,1); if ($depth==0){$stmt.=$d}else{$out.=$d} next } $instr = 0 if $c eq $q; next }
  if ($c eq '"' || $c eq "'") { $instr = 1; $q = $c; next }
  if ($c eq '{') { if ($depth == 0) { $out .= $stmt; $stmt = '' } $depth++; next }
  if ($c eq '}') { $depth--; next }
  if ($depth == 0 && $c eq ';') { $out .= fix($stmt); $stmt = '' }
}
$out .= $stmt; print $out;

sub toplevel_split {   # split on commas outside () [] {} and strings
  my ($t) = @_; my @p; my $d = 0; my $cur = ''; my $in = 0; my $qq = '';
  for (my $k = 0; $k < length $t; $k++) {
    my $c = substr($t, $k, 1);
    if ($in) { $cur .= $c; if ($c eq '\\') { $k++; $cur .= substr($t,$k,1); next } $in = 0 if $c eq $qq; next }
    if ($c eq '"' || $c eq "'") { $in = 1; $qq = $c; $cur .= $c; next }
    $d++ if $c =~ /[(\[{]/; $d-- if $c =~ /[)\]}]/;
    if ($c eq ',' && $d == 0) { push @p, $cur; $cur = ''; next }
    $cur .= $c;
  }
  push @p, $cur; return @p;
}
sub fix {
  my ($st) = @_;
  (my $body = $st) =~ s/;\s*$//;
  return $st if $body =~ /^\s*typedef\b/;
  my @p = toplevel_split($body);
  return $st if @p < 2;
  return $st unless $p[0] =~ /=/;
  my ($lhs) = split /=/, $p[0], 2;
  return $st if $lhs =~ /\(/;
  $lhs =~ /^(.*?)(\**\s*\w+\s*(?:\[[^\]]*\]\s*)*)$/s or return $st;
  my $base = $1; my $first = substr($p[0], length $base);
  return $st if $base !~ /\w/;
  my $r = "$base$first;";
  $r .= " $base$_;" for @p[1..$#p];
  return $r;
}
