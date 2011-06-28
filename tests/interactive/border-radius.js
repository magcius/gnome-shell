/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Clutter = imports.gi.Clutter;
const St = imports.gi.St;

const UI = imports.testcommon.ui;

UI.init();
let stage = Clutter.Stage.get_default();
stage.width = 640;
stage.height = 480;

let vbox = new St.BoxLayout({ width: stage.width,
                              height: stage.height,
                              style: 'background: #ffee88;' });
stage.add_actor(vbox);

let scroll = new St.ScrollView();
vbox.add(scroll, { expand: true });

let box = new St.BoxLayout({ vertical: true,
                             style: 'padding: 10px;'
                                    + 'spacing: 20px;' });
scroll.add_actor(box);

function addTestCase(radii, useGradient, size) {
    let background;
    if (useGradient)
        background = 'background-gradient-direction: vertical;'
                     + 'background-gradient-start: white;'
                     + 'background-gradient-end: gray;';
    else
        background = 'background: white;';

    let style = 'border: 1px solid black; '
              + 'border-radius: ' + radii + ';'
              + 'padding: 5px;'
              + background;

    if (size !== undefined) {
        style += 'width: ' + size + ';';
        style += 'height: ' + size + ';';
    }

    box.add(new St.Label({ text: "border-radius:  " + radii + ";",
                           style: style }),
                         { x_fill: false });
}

// uniform backgrounds
addTestCase(" 0px  5px 10px 15px", false);
addTestCase(" 5px 10px 15px  0px", false);
addTestCase("10px 15px  0px  5px", false);
addTestCase("15px  0px  5px 10px", false);

// gradient backgrounds
addTestCase(" 0px  5px 10px 15px", true);
addTestCase(" 5px 10px 15px  0px", true);
addTestCase("10px 15px  0px  5px", true);
addTestCase("15px  0px  5px 10px", true);

// radius clamping
addTestCase("200px 200px 200px 200px", false, "400px");
addTestCase("200px 200px 0px 200px",   false, "400px");
addTestCase("100px 200px 400px 800px", false, "400px");
addTestCase("800px 400px 200px 100px", false, "400px");

// radius clamping w/ gradient
addTestCase("200px 200px 200px 200px", true, "400px");
addTestCase("200px 200px 0px 200px",   true, "400px");
addTestCase("100px 200px 400px 800px", true, "400px");

// no borders
addTestCase("0px 0px 0px 0px", false, "500px");
addTestCase("0px 0px 0px 0px", true,  "500px");

stage.show();
Clutter.main();
stage.destroy();
