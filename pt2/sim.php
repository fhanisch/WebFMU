<?php

  $projname = $_POST["projname"];
  $paramNames = array();
  $paramRefs = array();
  $paramValues = array();
  $varNames = array();
  $varRefs = array();

  $i=0;
  while ($_POST["pname".$i])
  {
    $paramNames[$i] = $_POST["pname".$i];
    $i++;
  }

  $i=0;
  while ($_POST["pref".$i])
  {
    $paramRefs[$i] = $_POST["pref".$i];
    $i++;
  }

  $i=0;
  while ($_POST["pval".$i])
  {
    $paramValues[$i] = $_POST["pval".$i];
    $i++;
  }

  $i=0;
  while ($_POST["vname".$i])
  {
    $varNames[$i] = $_POST["vname".$i];
    $i++;
  }

  $i=0;
  while ($_POST["vref".$i])
  {
    $varRefs[$i] = (string)($_POST["vref".$i] - 1);
    $i++;
  }

  if ($i==0) exit("<p>No output values selected!</p>");

  simulate($projname, $paramRefs, $paramValues, $paramNames, $varRefs, $varNames);
  echo "<p> Simulation succesfully terminated.</p>";
  echo "<div id='plot'></div>";
?>
