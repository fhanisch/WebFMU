<?php
  $name = $_GET["n"];
  $tau = $_GET["T"];
  echo "huhu";
  simulate($name, $tau);
  echo "<button type='button' onclick='plot_table()'>Show Table</button>";
  echo "<div id='table'></div>";
  echo my_function();
?>
