<!doctype html>
<html lang="de">

  <head>
    <title>Simulation</title>
    <meta charset="utf-8">
    <link rel="stylesheet" href="index.css"/>
    <!--<script type="text/javascript" src="../MathJax/MathJax.js?config=TeX-AMS_HTML"></script>-->
    <script type="text/javascript" async
      src="https://cdnjs.cloudflare.com/ajax/libs/mathjax/2.7.1/MathJax.js?config=TeX-MML-AM_CHTML">
    </script>
    <script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
  </head>

  <body>

    <header>
      <h1>Simulation</h1>
    </header>

    <main>
      <h2>PT1-Glied</h2>
      <p ID="formel">
        \begin{equation}
        \ddot{x} = \frac{1}{c_2} (k-c_1\dot{x}-x)
        \end{equation}
      </p>
      <div class=settings>
        <form action="index.php?arg=test" method="post">
          <label for="projektname">Projektname</label>
          <input id="projektname" type="text" name="projektname" value="proj1"/>
          <label for="tau">Tau</label>
          <input id="tau" type="text" name="tau" value="0.01"/>
          <input type="Submit" value="Simulate"/>
        </form>
      </div>
      <?php
        $projektname = $_POST["projektname"];
        $tau = $_POST["tau"];
        if ( $_GET['arg'] <> "" )
        {
          simulate($projektname, $tau);
        }
      ?>

      <p>
        <a href="/simulation">Neu</a>
        <a href="/simulation/data">Daten</a>
      </p>
    </main>

    <footer>
      <p>FH 2017</p>
    </footer>

  </body>

</html>
