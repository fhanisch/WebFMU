<?php

  $projname = $_POST["projname"];
  $paramNames = array();
  $paramRefs = array();
  $paramValues = array();

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

  simulate($projname, $paramRefs, $paramValues, $paramNames);
  echo "<p> Simulation succesfully terminated.</p>";
  echo "<div id='plot'></div>";
?>
