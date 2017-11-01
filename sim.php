<?php
  $name = $_GET["n"];
  $p1 = $_GET["p1"];
  $p2 = $_GET["p2"];
  $p3 = $_GET["p3"];
  simulate($name, $p1, $p2, $p3);
  echo "<p> Simulation succesfully terminated.</p>";
  echo "<div id='plot'></div>";
?>
