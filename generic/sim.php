<?php

  $paramRefs = array();
  $paramValues = array();

  $i=0;
  while ($_POST["pref".$i])
  {
    $paramRefs[$i] = $_POST["pref".$i];
    echo $i.": ".$paramRefs[$i] , "<br>";
    $i++;
  }

  $i=0;
  while ($_POST["pval".$i])
  {
    $paramValues[$i] = $_POST["pval".$i];
    echo $i.": ".$paramValues[$i] , "<br>";
    $i++;
  }

  testArray($paramRefs,$paramValues);

?>
